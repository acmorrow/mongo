/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */


/************************************************************************************************

# Replication Internals

Replication is the set of systems used to continuously copy data from a primary server to secondary
servers so if the primary server fails a secondary server can take over soon. This process is
intended to be mostly transparent to the user, with drivers taking care of routing queries to the
requested replica. Replication in MongoDB is facilitated through [**replica
sets**](https://docs.mongodb.com/manual/replication/).

Replica sets are a group of nodes with one primary and multiple secondaries. The primary is
responsible for all writes. Users may specify that reads from secondaries are acceptable with a
`slaveOK` flag, but they are not by default.

# Steady State Replication

The normal running of a replica set is referred to as steady state replication. This is when there
is one primary and multiple secondaries. Each secondary is replicating data from the primary, or
another secondary off of which it is **chaining**.

## Life as a Primary

### Doing a Write

When a user does a write, all a primary node does is apply the write to the database like a
standalone would. The one difference from a standalone write is that replica set nodes have an
`OpObserver` that inserts a document to the **oplog** whenever a write to the database happens,
describing the write. The oplog is a capped collection called `oplog.rs` in the `local` database.
There are a few optimizations made for it in WiredTiger, and it is the only collection that doesn't
include an _id field.

If a write does multiple operations, each will have its own oplog entry; for example, inserts with
implicit collection creation create two oplog entries, one for the `create` and one for the
`insert`.

These entries are rewritten from the initial operation to make them idempotent; for example, updates
with `$inc` are changed to use `$set`.

Secondaries drive oplog replication via a pull process.

Writes can also specify a [**write
concern**](https://docs.mongodb.com/manual/reference/write-concern/). If a command includes a write
concern, the command will just block in its own thread until the oplog entries it generates have
been replicated to the requested number of nodes. The primary keeps track of how up-to-date the
secondaries are to know when to return. A write concern can specify a number of nodes to wait for,
or **majority**. If **majority** is specified, the write waits for that write to be in the
**committed snapshot** as well, so that it can be read with `readConcern: { level: majority }`
reads. (If this last sentence made no sense, come back to it at the end).

## Life as a Secondary

In general, secondaries just choose a node to sync from, their **sync source**, and then pull
operations from its oplog and apply those oplog entries to their own copy of the data on disk.

Secondaries also constantly update their sync source with their progress so that the primary can
satisfy write concerns.

### Oplog Fetching

A secondary keeps its data synchronized with its sync source by fetching oplog entries from its sync
source. This is done via the
[`OplogFetcher`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/oplog_fetcher.h).

The `OplogFetcher` first sends a `find` command to the sync source's oplog, and then follows with a
series of `getMore`s on the cursor.

The `OplogFetcher` makes use of the
[`Fetcher`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/client/fetcher.h) for this task,
which is a generic class used for fetching data from a collection on a remote node. A `Fetcher` is
given a `find` command and then follows that command with `getMore` requests. The `Fetcher` also
takes in a callback function that is called with the results of every batch.

Let’s refer to the sync source as node A and the fetching node as node B.

The `find` command that B’s `OplogFetcher` first sends to sync source A has a greater than or equal
predicate on the timestamp of the last oplog entry it has fetched. The original `find` command
should always return at least 1 document due to the greater than or equal predicate. If it does not,
that means that the A’s oplog is behind B's and thus A should not be B’s sync source. If it does
return a non-empty batch, but the first document returned does not match the last entry in B’s
oplog, that means that B's oplog has diverged from A's and it should go into
[**ROLLBACK**](https://docs.mongodb.com/manual/core/replica-set-rollbacks/).

After getting the original `find` response, secondaries check the metadata that accompanies the
response to see if the sync source is still a good sync source. Secondaries check that the node has
not rolled back since it was chosen and that it is still ahead of them.

The `OplogFetcher` uses **long-polling**. It specifies `awaitData: true, tailable: true` so that the
`getMore`s block until their `maxTimeMS` expires waiting for more data instead of returning
immediately. If there is no data to return at the end of `maxTimeMS`, the `OplogFetcher` receives an
empty batch and simply issues another `getMore`.

If any fetch requests have an error, then the `OplogFetcher` creates a new `Fetcher`. It restarts
the `Fetcher` with a new `find` command each time it receives an error for a maximum of 3 retries.
If it expires its retries then the `OplogFetcher` shuts down with an error status.

The `OplogFetcher` is owned by the
[`BackgroundSync`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/bgsync.h) thread.
The `BackgroundSync` thread runs continuously while a node is in SECONDARY state. `BackgroundSync`
sits in a loop, where each iteration it first chooses a sync source with the `SyncSourceResolver`
and then starts up the `OplogFetcher`. When the `OplogFetcher` terminates, `BackgroundSync` restarts
sync source selection, exits, or goes into ROLLBACK depending on the return status. The
`OplogFetcher` could terminate because the first batch implies that a rollback is required, it could
receive an error from the sync source, or it could just be shut down by its owner, such as when
`BackgroundSync` itself is shut down.

The `OplogFetcher` does not directly apply the operations it retrieves from the sync source. Rather,
it puts them into a buffer (the **`OplogBuffer`**) and another thread is in charge of taking the
operations off the buffer and applying them. That buffer uses an in-memory blocking queue for steady
state replication; there is a similar collection-backed buffer used for initial sync.

### Sync Source Selection

Whenever a node starts initial sync, creates a new `BackgroundSync` (when it stops being primary),
or errors on its current `OplogFetcher`, it must get a new sync source. Sync source selection is
done by the
[`SyncSourceResolver`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/sync_source_resolver.h).

The `SyncSourceResolver` delegates the duty of choosing a "sync source candidate" to the
[**`ReplicationCoordinator`**](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/replication_coordinator.h),
which in turn asks the
[**`TopologyCoordinator`**](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/topology_coordinator.h)
to choose a new sync source.

#### Choosing a sync source candidate

To choose a new sync source candidate, the `TopologyCoordinator` first checks if the user requested
a specific sync source with the `replSetSyncFrom` command. In that case, the secondary chooses that
host as the sync source and resets its state so that it doesn’t use that requested sync source
again.

If **chaining** is disallowed, the secondary needs to sync from the primary, and chooses it as a
candidate.

Otherwise, it iterates through all of the nodes and sees which one is the best.

* First the secondary checks the `TopologyCoordinator`'s cached view of the replica set for the
  latest OpTime known to be on the primary. Secondaries do not sync from nodes whose newest oplog
  entry is more than
  [`maxSyncSourceLagSecs`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/topology_coordinator_impl.cpp#L227-L240)
  seconds behind the primary's newest oplog entry.
* Secondaries then loop through each node and choose the closest node that satisfies [various
  criteria](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/topology_coordinator_impl.cpp#L162-L363).
  “Closest” here is determined by the lowest ping time to each node.
* If no node satisfies the necessary criteria, then the `BackgroundSync` waits 1 second and restarts
  the sync source selection process.

#### Sync Source Probing

After choosing a sync source candidate, the `SyncSourceResolver` probes the sync source candidate to
make sure it actually is able to fetch from the sync source candidate’s oplog.

* If the sync source candidate has no oplog or there is an error, the secondary blacklists that sync
  source for some time and then tries to find a new sync source candidate.
* If the oldest entry in the sync source candidate's oplog is newer than the node's newest entry,
  then the node blacklists that sync source candidate as well because the candidate is too far
  ahead.
* During initial sync, rollback, or recovery from unclean shutdown, nodes will set a specific
  OpTime, **`minValid`**, that they must reach before it is safe to read from the node and before
  the node can transition into SECONDARY state. If the secondary has a `minValid`, then the sync
  source candidate is checked for that `minValid` entry.
* The sync source's **RollbackID** is also fetched to be checked after the first batch is returned
  by the `OplogFetcher`.

If the secondary is too far behind all possible sync source candidates then it goes into maintenance
mode and waits for manual intervention (likely a call to `resync`). If no viable candidates were
found, `BackgroundSync` waits 1 second and attempts the entire sync source selection process again.
Otherwise, the secondary found a sync source! At that point `BackgroundSync` starts an OplogFetcher.

### Oplog Entry Application

A separate thread,
[`RSDataSync`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/rs_sync.h) is used for
pulling oplog entries off of the oplog buffer and applying them. `RSDataSync` constructs a
[`SyncTail`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/sync_tail.h) in a loop
which is used to actually apply the operations. The `SyncTail` instance does some oplog application,
and terminates when there is a state change where we need to pause oplog application. After it
terminates, `RSDataSync` loops back and decides if it should make a new `SyncTail` and continue.

`SyncTail` creates multiple threads that apply buffered oplog entries in parallel. Operations are
pulled off of the oplog buffer in batches to be applied. Nodes keep track of their “last applied
OpTime”, which is only moved forward at the end of a batch. Oplog entries within the same batch are
not necessarily applied in order. Operations on a document must be atomic and ordered, so operations
on the same document will be put on the same thread to be serialized. Additionally, command
operations are done serially in batches of size 1. Insert operations are also batched together for
improved performance.

## Replication and Topology Coordinators

The `ReplicationCoordinator` is the public api that replication presents to the rest of the code
base. It is in charge of coordinating the interaction of replication with the rest of the system.

The `ReplicationCoordinator` communicates with the storage layer and other nodes through the
[`ReplicationCoordinatorExternalState`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/replication_coordinator_external_state.h).
The external state also manages and owns all of the replication threads.

The `TopologyCoordinator` is in charge of maintaining state about the topology of the cluster. It is
non-blocking and does a large amount of a node's decision making surrounding replication. Most
replication command requests and responses are filled in here.

Both coordinators maintain views of the entire cluster and the state of each node, though there are
plans to merge these together.

## Communication

Each node has a copy of the **`ReplicaSetConfig`** in the `ReplicationCoordinator` that lists all
nodes in the replica set. This config lets each node talk to every other node.

Each node uses the internal client, the legacy c++ driver code in the
[`src/mongo/client`](https://github.com/mongodb/mongo/tree/r3.4.2/src/mongo/client) directory, to
talk to each other node. Nodes talk to each other by sending a mixture of external and internal
commands over the same incoming port as user commands. All commands take the same code path as
normal user commands. For security, nodes use the keyfile to authenticate to each other. You need to
be the system user to run replication commands, so nodes authenticate as the system user when
issuing remote commands to other nodes.

Each node communicates with other nodes at regular intervals to:

* Check the liveness of the other nodes (heartbeats)
* Stay up to date with the primary (oplog fetching)
* Update their sync source with their progress (`replSetUpdatePosition` commands)

Each oplog entry is assigned an `OpTime` to describe when it occurred so other nodes can compare how
up-to-date they are.

OpTimes include a timestamp and a term field. The term field indicates how many elections have
occurred since the replica set started.

The election protocol, known as
[protocol version 1 or PV1](https://docs.mongodb.com/manual/reference/replica-set-protocol-versions/),
is built on top of [Raft](https://raft.github.io/raft.pdf), so it is guaranteed that two primaries
will not be elected in the same term. This helps differentiate ops that occurred at the same time
but from different primaries in the case of a network partition.

### Oplog Fetcher Responses

The `OplogFetcher` just issues normal `find` and `getMore` commands, so the upstream node (the sync
source) does not get any information from the request. In the response, however, the downstream
node, the one that issues the `find` to its sync source, gets metadata that it uses to update its
view of the replica set.

There are two types of metadata, `ReplSetMetadata` and `OplogQueryMetadata`. (The
`OplogQueryMetadata` is new, so there is some temporary field duplication for backwards
compatibility.)

#### ReplSetMetadata

`ReplSetMetadata` comes with all replication commands and is processed similarly for all commands.
It includes:

1. The upstream node's last committed OpTime
2. The current term.
3. The `ReplicaSetConfig` version (this is used to determine if a reconfig has occurred on the
   upstream node that hasn't been registered by the downstream node yet).
4. The replica set ID.

If the metadata has a different config version than the downstream node's config version, then the
metadata is ignored until a reconfig command is received that synchronizes the config versions.

The node sets its term to the upstream node's term, and if it's a primary (which can only happen on
heartbeats), it steps down.

The last committed OpTime is only used in this metadata for
[arbiters](https://docs.mongodb.com/manual/core/replica-set-arbiter/), to advance their committed
OpTime and in sharding in some places. Otherwise it is ignored.

#### OplogQueryMetadata

`OplogQueryMetadata` only comes with `OplogFetcher` responses. It includes:

1. The upstream node's last committed OpTime. This is the most recent operation that would be
   reflected in the snapshot used for `readConcern: majority` reads.
2. The upstream node's last applied OpTime.
3. The index (as specified by the `ReplicaSetConfig`) of the node that the upstream node thinks is
   primary.
4. The index of the upstream node's sync source.

If the metadata says there is still a primary, the downstream node resets its election timeout into
the future.

The downstream node sets its last committed OpTime to the last committed OpTime of the upstream
node.

When it updates the last committed OpTime, it chooses a new committed snapshot if possible and tells
the storage engine to erase any old ones if necessary.

Before sending the next `getMore`, the downstream node uses the metadata to check if it should
change sync sources.

### Heartbeats

At a default of every 2 seconds, the `HeartbeatInterval`, every node sends a heartbeat to every
other node with the `replSetHeartbeat` command. This means that the number of heartbeats increases
quadratically with the number of nodes and is the reasoning behind the 50 member limit in a replica
set. The data, `ReplSetHeartbeatArgsV1` that accompanies every heartbeat is:

1. `ReplicaSetConfig` version
2. The id of the sender in the `ReplSetConfig`
3. Term
4. Replica set name
5. Sender host address

When the remote node receives the heartbeat, it first processes the heartbeat data, and then sends a
response back. First, the remote node makes sure the heartbeat is compatible with its replica set
name and its `ReplicaSetConfig` version and otherwise sends an error.

The receiving node's `TopologyCoordinator` updates the last time it received a heartbeat from the
sending node for liveness checking in its `MemberHeartbeatData` list.

If the sending node's config is higher than the receiving node's, then the receiving node schedules
a heartbeat to get the config. The receiving node's `ReplicationCoordinator` also updates its
`SlaveInfo` with the last update from the sending node and marks it as being up.

It then creates a `ReplSetHeartbeatResponse` object. This includes:

1. Replica set name
2. The receiving node's election time
3. The receiving node's last applied OpTime
4. The receiving node's last durable OpTime
5. The node the receiving node thinks is primary
6. The term of the receiving node
7. The state of the receiving node
8. The receiving node's sync source
9. The receiving node's `ReplicaSetConfig` version

When the sending node receives the response to the heartbeat, it first processes its
`ReplSetMetadata` like before.

The sending node postpones its election timeout if it sees a primary.

The `TopologyCoordinator` updates its `HeartbeatData`. It marks if the receiving node is up or down.

The sending node's `TopologyCoordinator` then looks at the response and decides the next action to
take: no action, priority takeover, or reconfig,

The `ReplicationCoordinator` then updates the `SlaveInfo` for the receiving node with its most
recently acquired OpTimes.

If the sending node is primary, this updates the commit point if the sending node sees that a
majority of its nodes have reached a newer OpTime. Any threads blocking on a writeConcern are woken
up to check if they now fulfill their requested writeConcern.

The next heartbeat is scheduled and then the next action set by the `TopologyCoordinator` is
executed.

If the action was a priority takeover, then the node ranks all of the priorities in its config and
assigns itself a priority takeover timeout proportional to its rank. After that timeout expires the
node will check if it's eligible to run for election and if so will begin an election. The timeout
is simply: `(election timeout) * (priority rank + 1)`.

### Update Position Commands

The last way that replica set nodes regularly communicate with each other is through
`replSetUpdatePosition` commands. The `ReplicationCoordinatorExternalState` creates a
[**`SyncSourceFeedback`**](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/sync_source_feedback.h)
object at startup that is responsible for sending `replSetUpdatePosition` commands.

The `SyncSourceFeedback` starts a loop. In each iteration it first waits on a condition variable
that is notified whenever the `ReplicationCoordinator` discovers that a node in the replica set has
replicated more operations and become more up-to-date. It checks that it is not in PRIMARY or
STARTUP state before moving on.

It then gets the node's sync source and creates a
[**`Reporter`**](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/reporter.h) that
actually sends the `replSetUpdatePosition` command to the sync source. This command keeps getting
sent every `keepAliveInterval` milliseconds (`(electionTimeout / 2)`) to maintain liveness
information about the nodes in the replica set.

`replSetUpdatePosition` commands are the primary means of maintaining liveness. Thus, if the primary
cannot communicate directly with every node, but it can communicate with every node through other
nodes, it will still stay primary.

The `replSetUpdatePosition` command contains the following information:

1. An `optimes` array containing an object for each live replica set member. This information is
   filled in by the `ReplicationCoordinator` with information from its `SlaveInfo`. Nodes that are
   believed to be down are not included. Each node contains:

    1. last durable OpTime
    2. last applied OpTime
    3. memberId
    4. `ReplicaSetConfig` version

2. `ReplSetMetadata`. Usually this only comes in responses, but here it comes in the request as
   well.

When a node receives a `replSetUpdatePosition` command, the first thing it does is have the
`ReplicationCoordinator` process the `ReplSetMetadata` as before.

For every node’s OpTime data in the `optimes` array, the receiving node updates its view of the
replicaset in the replication and topology coordinators. This updates the liveness information of
every node in the `optimes` list. If the data is about the receiving node, it ignores it. If the
`ReplSetConfig` versions don’t match, it errors. If the receiving node is a primary and it learns
that the commit point should be moved forward, it does so.

If something has changed and the receiving node itself has a sync source, it forwards its new
information to its own sync source.

The `replSetUpdatePosition` command response does not include any information unless there is an
error, such as in a `ReplSetConfig` mismatch.

## Read Concern

MongoDB does not provide snapshot isolation. All reads in MongoDB are executed on snapshots of the
data taken at some point in time; however if the storage engine yields while executing a read, the
read may continue on a newer snapshot. Thus, reads are currently never guaranteed to return all data
from one point in time. This means that some documents can be skipped if they are updated and any
updates that occurred since the read began may or may not be seen.

[Read concern](https://docs.mongodb.com/manual/reference/read-concern/) is an option sent with any
read command to specify at what consistency level the read should be satisfied. There are 3 read
concern levels:

* Local
* Majority
* Linearizable

**Local** just returns whatever the most up-to-date data is on the node. It does this by reading
from the storage engine’s most recent snapshot(s).

**Majority** uses the last committed snapshot(s) to do its read. The data read only reflects the
oplog entries that have been replicated to a majority of nodes in the replica set. Any data seen in
majority reads cannot roll back in the future. Thus majority reads prevent **dirty reads**, though
they often are **stale reads**.

Read concern majority reads usually return as fast as local reads, but sometimes will block. Read
concern majority reads do not wait for anything to be committed; they just use different snapshots
from local reads. They do block though when the node metadata (in the catalog cache) differs from
the committed snapshot. For example, index builds or drops, collection creates or drops, database
drops, or collmod’s could cause majority reads to block. If the primary receives a `createIndex`
command, subsequent majority reads will block until that index build is finished on a majority of
nodes. Majority reads also block right after startup or rollback when we do not yet have a committed
snapshot.

MongoDB continuously directs the storage engine to take named snapshots. Reads with read concern
level local are executed on “unnamed snapshots,” which are ephemeral and exist only long enough to
satisfy the read transaction. As a node discovers that its writes have been replicated to
secondaries, it updates its committed OpTime. The newest named snapshot older than the commit point
becomes the new "committed snapshot" used for read majority reads. Any named snapshots older than
the "committed snapshot" are then cleaned up (deleted). MongoDB tells WiredTiger to save up to 1000
named snapshots at a time. If the commit point doesn't move, but writes continue to happen, we will
keep taking more snapshots and may hit the limit. Afterwards, no further snapshots are created until
the commit point moves and old snapshots are deleted. The commit level might not move if you are
doing w:1 writes with an arbiter, for example. If we hit the limit, but continue to take writes, we
may create a large gap across the oplog entries where there is no associated named snapshot. When
the commit point begins to move forward again and we start deleting old snapshots again, the next
snapshots will occur at the most recent OpTime and not be able to fill in the gap. In this case,
once the commit point moves ahead into the gap, the committed snapshot will remain before the gap,
and majority reads will read increasingly stale data until the commit point gets to the end of the
gap. To reduce the chance of hitting the snapshot limit and this happening, we slow down the
frequency with which we mark snapshots as “named snapshots” as we get closer to the limit.

**Linearizable** read concern actually does block for some time. Linearizability guarantees that if
one thread does a write that is acknowledged and tells another thread about that write, then that
second thread should see the write. If you transiently have 2 primaries (one has yet to step down)
and you read the data from the old primary, the new one may have newer data and you may get a stale
read.

To prevent reading from stale primaries, reads block to ensure that the current node remains the
primary after the read is complete. Nodes just write a noop to the oplog and wait for it to be
replicated to a majority of nodes. The node reads data from the most recent snapshot, and then the
noop write occurs after the fact. Thus, since we wait for the noop write to be replicated to a
majority of nodes, linearizable reads satisfy all of the same guarantees of read concern majority,
and then some. Linearizable read concern reads are only done on the primary, and they only apply to
single document reads, since linearizability is only defined as a property on single objects.

**afterOpTime** is another read concern option, only used internally, only for config servers as
replica sets. **Read after optime** means that the read will block until the node has replicated
writes after a certain OpTime. This means that if read concern local is specified it will wait until
the local snapshot is beyond the specified OpTime. If read concern majority is specified it will
wait until the committed snapshot is beyond the specified OpTime. In 3.6 this feature will be
extended to support a sharded cluster and use a **Lamport Clock** to provide **causal consistency**.

# Elections

## Step Up

There are a number of ways that a node will run for election:
* If it hasn't seen a primary within the election timeout (which defaults to 10 seconds).
* If it realizes that it has higher priority than the primary, it will wait and run for
  election (also known as a **priority takeover**). The amount of time the node waits before calling
  an election is directly related to its priority in comparison to the priority of rest of the set
  (so higher priority nodes will call for a priority takeover faster than lower priority nodes).
  Priority takeovers allow users to specify a node that they would prefer be the primary.
* Newly elected primaries attempt to catchup to the latest applied OpTime in the replica
  set. Until this process (called primary catchup) completes, the new primary will not accept
  writes. If a secondary realizes that it is more up-to-date than the primary and the primary takes
  longer than `catchUpTakeoverDelayMillis` (default 30 seconds), it will run for election. This
  behvarior is known as a **catchup takeover**. If primary catchup is taking too long, catchup
  takeover can help allow the replica set to accept writes sooner, since a more up-to-date node will
  not spend as much time (or any time) in catchup. See the "Transitioning to PRIMARY" section for
  further details on primary catchup.
* The `replSetStepUp` command can be run on an eligible node to cause it to run for election
  immediately. We don't expect users to call this command, but it is run internally for election
  handoff and testing.
* When a node is stepped down via the `replSetStepDown` command, if the `enableElectionHandoff`
  parameter is set to true (the default), it will choose an eligible secondary to run the
  `replSetStepUp` command on a best-effort basis. This behavior is called **election handoff**. This
  will mean that the replica set can shorten failover time, since it skips waiting for the election
  timeout. If `replSetStepDown` was called with `force: true` or the node was stepped down while
  `enableElectionHandoff` is false, then nodes in the replica set will wait until the election
  timeout triggers to run for election.


### Candidate Perspective

A candidate node first runs a dry-run election. In a **dry-run election**, a node starts a
[`VoteRequester`](https://github.com/mongodb/mongo/blob/r4.2.0/src/mongo/db/repl/vote_requester.h),
which uses a
[`ScatterGatherRunner`](https://github.com/mongodb/mongo/blob/r4.2.0/src/mongo/db/repl/scatter_gather_runner.h)
to send a `replSetRequestVotes` command to every node asking if that node would vote for it. The
candidate node does not increase its term during a dry-run because if a primary ever sees a higher
term than its own, it steps down. By first conducting a dry-run election, we make it unlikely that
nodes will increase their own term when they would not win and prevent needless primary stepdowns.
If the node fails the dry-run election, it just continues replicating as normal. If the node wins
the dry-run election, it begins a real election.

If the candidate was stepped up as a result of an election handoff, it will skip the dry-run and
immediately call for a real election.

In the real election, the node first increments its term and votes for itself. It then follows the
same process as the dry-run to start a `VoteRequester` to send a `replSetRequestVotes` command to
every single node. Each node then decides if it should vote "aye" or "nay" and responds to the
candidate with their vote. The candidate node must be at least as up to date as a majority of voting
members in order to get elected.

If the candidate received votes from a majority of nodes, including itself, the candidate wins the
election.

### Voter Perspective

When a node receives a `replSetRequestVotes` command, it first checks if the term is up to date and
updates its own term accordingly. The `ReplicationCoordinator` then asks the `TopologyCoordinator`
if it should grant a vote. The vote is rejected if:

1. It's from an older term.
2. The config versions do not match.
3. The replica set name does not match.
4. The last applied OpTime that comes in the vote request is older than the voter's last applied
   OpTime.
5. If it's not a dry-run election and the voter has already voted in this term.
6. If the voter is an arbiter and it can see a healthy primary of greater or equal priority. This is
   to prevent primary flapping when there are two nodes that can't talk to each other and an arbiter
   that can talk to both.

Whenever a node votes for itself, or another node, it records that "LastVote" information durably to
the `local.replset.election` collection. This information is read into memory at startup and used in
future elections. This ensures that even if a node restarts, it does not vote for two nodes in the
same term.

### Transitioning to PRIMARY

Now that the candidate has won, it must become PRIMARY. First it clears its sync source and notifies
all nodes that it won the election via a round of heartbeats. Then the node checks if it needs to
catch up from the former primary. Since the node can be elected without the former primary's vote,
the primary-elect will attempt to replicate any remaining oplog entries it has not yet replicated
from any viable sync source. While these are guaranteed to not be committed, it is still good to
minimize rollback when possible.

The primary-elect uses the responses from the recent round of heartbeats to see the latest applied
OpTime of every other node. If the primary-elect’s last applied OpTime is less than the newest last
applied OpTime it sees, it will set that as its target OpTime to catch up to. At the beginning of
catchup, the primary-elect will schedule a timer for the catchup-timeout. If that timeout expires or
if the node reaches the target OpTime, then the node ends the catch-up phase. The node then clears
its sync source and stops the `OplogFetcher`.

We will ignore whether or not **chaining** is enabled for primary catchup so that the primary-elect
can find a sync source. And one thing to note is that the primary-elect will not necessarily sync
from the most up-to-date node, but its sync source will sync from a more up-to-date node. This will
mean that the primary-elect will still be able to catchup to its target OpTime. Since catchup is
best-effort, it could time out before the node has applied operations through the target OpTime.
Even if this happens, the primary-elect will not step down.

At this point, whether catchup was successful or not, the node goes into "drain mode". This is when
the node has already logged "transition to PRIMARY", but has not yet applied all of the oplog
entries in its oplog buffer. `replSetGetStatus` will now say the node is in PRIMARY state. The
applier keeps running, and when it completely drains the buffer, it signals to the
`ReplicationCoordinator` to finish the step up process. The node marks that it can begin to accept
writes. According to the Raft Protocol, we cannot update the commit point to reflect oplog entries
from previous terms until the commit point is updated to reflect an oplog entry in the current term.
The node writes a "new primary" noop oplog entry so that it can commit older writes as soon as
possible. Once the commit point is updated to reflect the "new primary" oplog entry, older writes
will automatically be part of the commit point by nature of happening before the term change.
Finally, the node drops all temporary collections and logs “transition to primary complete”.

## Step Down

When a `replSetStepDown` command comes in, the node begins to check if it can step down. First, the
node kills all user operations and they return an error to the user. Then the node loops trying to
step down. It repeatedly checks if a majority of nodes are caught up and if one of those caught up
nodes is electable or if the user requested it to force a stepdown. It then begins to step down.

Stepdowns also occur if a primary sees a higher term than themselves or if a primary stops being
able to transitively communicate with a majority of nodes. The primary does not need to be able to
communicate directly with a majority of nodes. If primary A can’t communicate with node B, but A can
communicate with C which can communicate with B, that is okay. If you consider the minimum spanning
tree on the cluster where edges are connections from nodes to their sync source, then as long as the
primary is connected to a majority of nodes, it will stay primary.

Once the node begins to step down, it first sets its state to `follower` in the
`TopologyCoordinator`. It then transitioning to SECONDARY in the `ReplicationCoordinator`.

# Rollback

Rollback is the process whereby a node that diverges from its sync source gets back to a consistent
point in time on the sync source's branch of history. We currently support two rollback algorithms,
Recover To A Timestamp (RTT) and Rollback via Refetch. This section will cover the RTT method.

Situations that require rollback can occur due to network partitions. Consider a scenario where a
secondary can no longer hear from the primary and subsequently runs for an election. We now have
two primaries that can both accept writes, creating two different branches of history (one of the
primaries will detect this situation soon and step down). If the smaller half, meaning less than a
majority of the set, accepts writes during this time, those writes will be uncommitted. A node with
uncommitted writes will roll back its changes and roll forward to match its sync source. Note that a
rollback is not necessary if there are no uncommitted writes.

As of 4.0, Replication supports the [`Recover To A Timestamp`](https://github.com/mongodb/mongo/blob/r4.2.0/src/mongo/db/repl/rollback_impl.h#L158)
algorithm (RTT), in which a node recovers to a consistent point in time and applies operations until
it catches up to the sync source's branch of history. RTT uses the WiredTiger storage engine to
recover to a stable timestamp, which is the highest timestamp at which the storage engine can take
a checkpoint. This can be considered a consistent, majority committed point in time for replication
and storage.

A node goes into rollback when its last fetched OpTime is greater than its sync source's last
applied OpTime, but it is in a lower term. In this case, the `OplogFetcher` will return an empty
batch and fail with an `OplogStartMissing` error.

During [rollback](https://github.com/mongodb/mongo/blob/r4.2.0/src/mongo/db/repl/rollback_impl.cpp#L176),
nodes first transition to the `ROLLBACK` state and kill all user operations to ensure that we can
successfully acquire the RSTL. (TODO SERVER-43789: link to RSTL section) Reads are prohibited while
we are in the `ROLLBACK` state.

We then wait for background index builds to complete before finding the `common point` between the
rolling back node and the sync source node. The `common point` is the OpTime after which the nodes'
oplogs start to differ. During this step, we keep track of the operations that are rolled back up
until the `common point` and update necessary data structures. This includes metadata that we may
write out to rollback files and and use to roll back collection fast-counts. Then, we increment
the Rollback ID (RBID), a monotonically increasing number that is incremented every time a rollback
occurs. We can use the RBID to check if a rollback has occurred on our sync source since the
baseline RBID was set.

Now, we enter the data modification section of the rollback algorithm, which begins with
aborting prepared transactions and ends with reconstructing them at the end. If we fail at any point
during this phase, we must terminate the rollback attempt because we cannot safely recover.

Before we actually recover to the `stableTimestamp`, we must abort the storage transaction of any
prepared transaction. In doing so, we release any resources held by those transactions and
invalidate any in-memory state we recorded.

If `createRollbackDataFiles` was set to `true` (the default), we begin writing rollback files for
our rolled back documents. It is important that we do this after we abort any prepared transactions
in order to avoid unnecessary prepare conflicts when trying to read documents that were modified by
those transactions, which must be aborted for rollback anyway. Finally, if we have rolled back any
operations, we invalidate all sessions on this server.

Now, we are ready to tell the storage engine to recover to the last stable timestamp. Upon success,
the storage engine restores the data reflected in the database to the data reflected at the last
stable timestamp. This does not, however, revert the oplog. In order to revert the oplog, rollback
must remove all oplog entries after the `common point`. This is called the truncate point and is
written into the `oplogTruncateAfterPoint` document. Now, the recovery process knows where to
truncate the oplog on the rollback node.

During the last few steps of the data modification section, we clear the state of the
`DropPendingCollectionReaper`, which manages collections that are marked as drop-pending by the Two
Phase Drop algorithm, and make sure it aligns with what is currently on disk. After doing so, we can
run through the oplog recovery process, which truncates the oplog after the `common point` (at the
truncate point) and applies all oplog entries through the end of the sync source's oplog.

The last thing we do before exiting the data modification section is reconstruct prepared
transactions. (TODO SERVER-43783: add link to prepared transactions recovery process). We must also
restore their in-memory state to what it was prior to the rollback in order to fulfill the
durability guarantees of prepared transactions.

At this point, the last applied and durable OpTimes still point to the divergent branch of history,
so we must update them to be at the top of the oplog, which should be the `common point`.

Now, we can trigger the rollback `OpObserver` and notify any external subsystems that a rollback has
occurred. For example, the config server must update its shard registry in order to make sure it
does not have data that has just been rolled back. Finally, we log a summary of the rollback process
and transition to the `SECONDARY` state. This transition must succeed if we ever entered the
`ROLLBACK` state in the first place. Otherwise, we shut down.

# Initial Sync

Initial sync is the process that we use to add a new node to a replica set. Initial sync is
initiated by the `ReplicationCoordinator` and done in the
[**`DataReplicator`**](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/data_replicator.h).
When a node begins initial sync or `resync` is called, it goes into `STARTUP2` state. `STARTUP` is
reserved for the time before initial sync when a node may need to recover from unclean shutdown.

The `DataReplicator` first gets a sync source. Second, the node drops all of its data except for the
local database and recreates the oplog. It then gets the Rollback ID from the sync source to ensure
at the end that no rollbacks occurred during initial sync. Finally, it creates an `OplogFetcher` and
starts fetching and buffering oplog entries from the sync source to be applied later. Operations are
buffered to a collection so that they are not limited by the amount of memory available.

#### Data clone phase

The new node then begins to clone data from its sync source. The `DataReplicator` constructs a
[`DatabasesCloner`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/databases_cloner.h)
that's used to clone all of the databases on the upstream node. The `DatabasesCloner` asks the sync
source for a list of its databases and then for each one it creates a
[`DatabaseCloner`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/database_cloner.h)
to clone that database. Each `DatabaseCloner` asks the sync source for a list of its collections and
then creates a
[`CollectionCloner`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/collection_cloner.h)
to clone that collection. The `CollectionCloner` calls `listIndexes` on the sync source and creates
a
[`CollectionBulkLoader`](https://github.com/mongodb/mongo/blob/r3.4.2/src/mongo/db/repl/collection_bulk_loader.h)
to create all of the indexes in parallel with the data cloning. The `CollectionCloner` then just
runs `find` and `getMore` requests on the sync source repeatedly, inserting the fetched documents
each time, until it fetches all of the documents.

#### Oplog application phase

After the cloning phase of initial sync has finished, the oplog application phase begins. The new
node first asks its sync source for its last applied OpTime and this is saved as `minValid`, the
oplog entry it must apply before it's consistent and can become a secondary.

The new node iterates through all of the buffered operations and applies them to the data on disk.
Oplog entries continue to be fetched and added to the buffer while this is occurring.

If an error occurs on application of an entry, it retries the operation by fetching the entire
document from the source and just replacing the local document with that one. The last applied
OpTime is again fetched from the sync source and `minValid` is pushed back to this new OpTime. This
can occur if a document that needs to be updated was deleted before it was cloned, so the `update`
op refers to a document that does not exist on the initial syncing node.

#### Idempotency concerns

Some of the operations that are applied may already be reflected in the data that was cloned since
we started buffering oplog entries before the collection cloning phase even started. Consider the
following:

1. Start buffering oplog entries
2. Insert `{a: 1, b: 1}` to collection `foo`
3. Insert `{a: 1, b: 2}` to collection `foo`
5. Drop collection `foo`
6. Recreate collection `foo`
7. Create unique index on field `a` in collection `foo`
8. Clone collection `foo`
9. Start applying oplog entries and try to insert both `{a: 1, b: 1}` and `{a: 1, b: 2}`

As seen here, there can be operations on collections that have since been dropped or indexes could
conflict with the data being added. As a result, many errors that occur here are ignored and assumed
to resolve themselves. If known problematic operations such as `renameCollection` are received,
where we cannot assume a drop will come and fix them, we abort and retry initial sync.

#### Finishing initial sync

The oplog application phase concludes when the node applies `minValid`. The node checks its sync
source's Rollback ID to see if a rollback occurred and if so, restarts initial sync. Otherwise, the
`DataReplicator` shuts down and the `ReplicationCoordinator` starts steady state replication.
 */


#pragma once

#include <cstdlib>
#include <string>
#include <typeinfo>

#include "mongo/base/status.h"  // NOTE: This is safe as utils depend on base
#include "mongo/base/status_with.h"
#include "mongo/platform/compiler.h"
#include "mongo/util/concurrency/thread_name.h"
#include "mongo/util/debug_util.h"

#define MONGO_INCLUDE_INVARIANT_H_WHITELISTED
#include "mongo/util/invariant.h"
#undef MONGO_INCLUDE_INVARIANT_H_WHITELISTED

namespace mongo {

class AssertionCount {
public:
    AssertionCount();
    void rollover();
    void condrollover(int newValue);

    AtomicWord<int> regular;
    AtomicWord<int> warning;
    AtomicWord<int> msg;
    AtomicWord<int> user;
    AtomicWord<int> rollovers;
};

extern AssertionCount assertionCount;


class DBException;
std::string causedBy(const DBException& e);
std::string causedBy(const std::string& e);

/** Most mongo exceptions inherit from this; this is commonly caught in most threads */
class DBException : public std::exception {
public:
    const char* what() const throw() final {
        return reason().c_str();
    }

    virtual void addContext(StringData context) {
        _status.addContext(context);
    }

    Status toStatus(StringData context) const {
        return _status.withContext(context);
    }
    const Status& toStatus() const {
        return _status;
    }

    virtual std::string toString() const {
        return _status.toString();
    }

    const std::string& reason() const {
        return _status.reason();
    }

    ErrorCodes::Error code() const {
        return _status.code();
    }

    std::string codeString() const {
        return _status.codeString();
    }

    /**
     * Returns true if this DBException's code is a member of the given category.
     */
    template <ErrorCategory category>
    bool isA() const {
        return ErrorCodes::isA<category>(*this);
    }

    /**
     * Returns the generic ErrorExtraInfo if present.
     */
    const ErrorExtraInfo* extraInfo() const {
        return _status.extraInfo();
    }

    /**
     * Returns a specific subclass of ErrorExtraInfo if the error code matches that type.
     */
    template <typename ErrorDetail>
    const ErrorDetail* extraInfo() const {
        return _status.extraInfo<ErrorDetail>();
    }

    static AtomicWord<bool> traceExceptions;

protected:
    DBException(const Status& status) : _status(status) {
        invariant(!status.isOK());
        traceIfNeeded(*this);
    }

private:
    static void traceIfNeeded(const DBException& e);

    /**
     * This method exists only to make all non-final types in this hierarchy abstract to prevent
     * accidental slicing.
     */
    virtual void defineOnlyInFinalSubclassToPreventSlicing() = 0;

    Status _status;
};

class AssertionException : public DBException {
public:
    AssertionException(const Status& status) : DBException(status) {}
};

/**
 * The base class of all DBExceptions for codes of the given ErrorCategory to allow catching by
 * category.
 */
template <ErrorCategory kCategory>
class ExceptionForCat : public virtual AssertionException {
protected:
    // This will only be called by subclasses, and they are required to instantiate
    // AssertionException themselves since it is a virtual base. Therefore, the AssertionException
    // construction here should never actually execute, but it is required to be present to allow
    // subclasses to construct us.
    ExceptionForCat() : AssertionException((std::abort(), Status::OK())) {
        invariant(isA<kCategory>());
    }
};


/**
 * This namespace contains implementation details for our error handling code and should not be used
 * directly in general code.
 */
namespace error_details {

template <ErrorCodes::Error kCode, typename... Bases>
class ExceptionForImpl final : public Bases... {
public:
    MONGO_STATIC_ASSERT(isNamedCode<kCode>);

    ExceptionForImpl(const Status& status) : AssertionException(status) {
        invariant(status.code() == kCode);
    }

    // This is only a template to enable SFINAE. It will only be instantiated with the default
    // value.
    template <ErrorCodes::Error code_copy = kCode>
    const ErrorExtraInfoFor<code_copy>* operator->() const {
        MONGO_STATIC_ASSERT(code_copy == kCode);
        return this->template extraInfo<ErrorExtraInfoFor<kCode>>();
    }

private:
    void defineOnlyInFinalSubclassToPreventSlicing() final {}
};

template <ErrorCodes::Error code, typename categories = ErrorCategoriesFor<code>>
struct ExceptionForDispatcher;

template <ErrorCodes::Error code, ErrorCategory... categories>
struct ExceptionForDispatcher<code, CategoryList<categories...>> {
    using type = std::conditional_t<sizeof...(categories) == 0,
                                    ExceptionForImpl<code, AssertionException>,
                                    ExceptionForImpl<code, ExceptionForCat<categories>...>>;
};

}  // namespace error_details


/**
 * Resolves to the concrete exception type for the given error code.
 *
 * It will be a subclass of both AssertionException, along with ExceptionForCat<> of every category
 * that the code belongs to.
 *
 * TODO in C++17 we can combine this with ExceptionForCat by doing something like:
 * template <auto codeOrCategory> using ExceptionFor = typename
 *      error_details::ExceptionForDispatcher<decltype(codeOrCategory)>::type;
 */
template <ErrorCodes::Error code>
using ExceptionFor = typename error_details::ExceptionForDispatcher<code>::type;

MONGO_COMPILER_NORETURN void verifyFailed(const char* expr, const char* file, unsigned line);
MONGO_COMPILER_NORETURN void invariantOKFailed(const char* expr,
                                               const Status& status,
                                               const char* file,
                                               unsigned line) noexcept;
MONGO_COMPILER_NORETURN void invariantOKFailedWithMsg(const char* expr,
                                                      const Status& status,
                                                      const std::string& msg,
                                                      const char* file,
                                                      unsigned line) noexcept;

#define fassertFailed MONGO_fassertFailed
#define MONGO_fassertFailed(...) ::mongo::fassertFailedWithLocation(__VA_ARGS__, __FILE__, __LINE__)
MONGO_COMPILER_NORETURN void fassertFailedWithLocation(int msgid,
                                                       const char* file,
                                                       unsigned line) noexcept;

#define fassertFailedNoTrace MONGO_fassertFailedNoTrace
#define MONGO_fassertFailedNoTrace(...) \
    ::mongo::fassertFailedNoTraceWithLocation(__VA_ARGS__, __FILE__, __LINE__)
MONGO_COMPILER_NORETURN void fassertFailedNoTraceWithLocation(int msgid,
                                                              const char* file,
                                                              unsigned line) noexcept;

#define fassertFailedWithStatus MONGO_fassertFailedWithStatus
#define MONGO_fassertFailedWithStatus(...) \
    ::mongo::fassertFailedWithStatusWithLocation(__VA_ARGS__, __FILE__, __LINE__)
MONGO_COMPILER_NORETURN void fassertFailedWithStatusWithLocation(int msgid,
                                                                 const Status& status,
                                                                 const char* file,
                                                                 unsigned line) noexcept;

#define fassertFailedWithStatusNoTrace MONGO_fassertFailedWithStatusNoTrace
#define MONGO_fassertFailedWithStatusNoTrace(...) \
    ::mongo::fassertFailedWithStatusNoTraceWithLocation(__VA_ARGS__, __FILE__, __LINE__)
MONGO_COMPILER_NORETURN void fassertFailedWithStatusNoTraceWithLocation(int msgid,
                                                                        const Status& status,
                                                                        const char* file,
                                                                        unsigned line) noexcept;

/* convert various types of exceptions to strings */
std::string causedBy(StringData e);
std::string causedBy(const char* e);
std::string causedBy(const DBException& e);
std::string causedBy(const std::exception& e);
std::string causedBy(const std::string& e);
std::string causedBy(const std::string* e);
std::string causedBy(const Status& e);

#define fassert MONGO_fassert
#define MONGO_fassert(...) ::mongo::fassertWithLocation(__VA_ARGS__, __FILE__, __LINE__)

/** aborts on condition failure */
inline void fassertWithLocation(int msgid, bool testOK, const char* file, unsigned line) {
    if (MONGO_unlikely(!testOK)) {
        fassertFailedWithLocation(msgid, file, line);
    }
}

template <typename T>
inline T fassertWithLocation(int msgid, StatusWith<T> sw, const char* file, unsigned line) {
    if (MONGO_unlikely(!sw.isOK())) {
        fassertFailedWithStatusWithLocation(msgid, sw.getStatus(), file, line);
    }
    return std::move(sw.getValue());
}

inline void fassertWithLocation(int msgid, const Status& status, const char* file, unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        fassertFailedWithStatusWithLocation(msgid, status, file, line);
    }
}

#define fassertNoTrace MONGO_fassertNoTrace
#define MONGO_fassertNoTrace(...) \
    ::mongo::fassertNoTraceWithLocation(__VA_ARGS__, __FILE__, __LINE__)
inline void fassertNoTraceWithLocation(int msgid, bool testOK, const char* file, unsigned line) {
    if (MONGO_unlikely(!testOK)) {
        fassertFailedNoTraceWithLocation(msgid, file, line);
    }
}

template <typename T>
inline T fassertNoTraceWithLocation(int msgid, StatusWith<T> sw, const char* file, unsigned line) {
    if (MONGO_unlikely(!sw.isOK())) {
        fassertFailedWithStatusNoTraceWithLocation(msgid, sw.getStatus(), file, line);
    }
    return std::move(sw.getValue());
}

inline void fassertNoTraceWithLocation(int msgid,
                                       const Status& status,
                                       const char* file,
                                       unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        fassertFailedWithStatusNoTraceWithLocation(msgid, status, file, line);
    }
}

namespace error_details {

// This function exists so that uassert/massert can take plain int literals rather than requiring
// ErrorCodes::Error wrapping.
template <typename StringLike>
Status makeStatus(int code, StringLike&& message) {
    return Status(ErrorCodes::Error(code), std::forward<StringLike>(message));
}

template <typename ErrorDetail,
          typename StringLike,
          typename = stdx::enable_if_t<
              std::is_base_of<ErrorExtraInfo, std::remove_reference_t<ErrorDetail>>::value>>
Status makeStatus(ErrorDetail&& detail, StringLike&& message) {
    return Status(std::forward<ErrorDetail>(detail), std::forward<StringLike>(message));
}

}  // namespace error_details

/**
 * Common implementation for assert and assertFailed macros. Not for direct use.
 *
 * Using an immediately invoked lambda to give the compiler an easy way to inline the check (expr)
 * and out-of-line the error path. This is most helpful when the error path involves building a
 * complex error message in the expansion of msg. The call to the lambda is followed by
 * MONGO_COMPILER_UNREACHABLE as it is impossible to mark a lambda noreturn.
 */
#define MONGO_BASE_ASSERT_FAILED(fail_func, code, msg)                                    \
    do {                                                                                  \
        [&]() MONGO_COMPILER_COLD_FUNCTION {                                              \
            fail_func(::mongo::error_details::makeStatus(code, msg), __FILE__, __LINE__); \
        }();                                                                              \
        MONGO_COMPILER_UNREACHABLE;                                                       \
    } while (false)

#define MONGO_BASE_ASSERT(fail_func, code, msg, cond)       \
    do {                                                    \
        if (MONGO_unlikely(!(cond))) {                      \
            MONGO_BASE_ASSERT_FAILED(fail_func, code, msg); \
        }                                                   \
    } while (false)

/**
 * "user assert".  if asserts, user did something wrong, not our code.
 * On failure, throws an exception.
 */
#define uasserted(msgid, msg) MONGO_BASE_ASSERT_FAILED(::mongo::uassertedWithLocation, msgid, msg)
#define uassert(msgid, msg, expr) \
    MONGO_BASE_ASSERT(::mongo::uassertedWithLocation, msgid, msg, expr)

MONGO_COMPILER_NORETURN void uassertedWithLocation(const Status& status,
                                                   const char* file,
                                                   unsigned line);

#define uassertStatusOK(...) ::mongo::uassertStatusOKWithLocation(__VA_ARGS__, __FILE__, __LINE__)
inline void uassertStatusOKWithLocation(const Status& status, const char* file, unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        uassertedWithLocation(status, file, line);
    }
}

template <typename T>
inline T uassertStatusOKWithLocation(StatusWith<T> sw, const char* file, unsigned line) {
    uassertStatusOKWithLocation(sw.getStatus(), file, line);
    return std::move(sw.getValue());
}

/**
 * Like uassertStatusOK(status), but also takes an expression that evaluates to  something
 * convertible to std::string to add more context to error messages. This contextExpr is only
 * evaluated if the status is not OK.
 */
#define uassertStatusOKWithContext(status, contextExpr) \
    ::mongo::uassertStatusOKWithContextAndLocation(     \
        status, [&]() -> std::string { return (contextExpr); }, __FILE__, __LINE__)
template <typename ContextExpr>
inline void uassertStatusOKWithContextAndLocation(const Status& status,
                                                  ContextExpr&& contextExpr,
                                                  const char* file,
                                                  unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        uassertedWithLocation(
            status.withContext(std::forward<ContextExpr>(contextExpr)()), file, line);
    }
}

template <typename T, typename ContextExpr>
inline T uassertStatusOKWithContextAndLocation(StatusWith<T> sw,
                                               ContextExpr&& contextExpr,
                                               const char* file,
                                               unsigned line) {
    uassertStatusOKWithContextAndLocation(
        sw.getStatus(), std::forward<ContextExpr>(contextExpr), file, line);
    return std::move(sw.getValue());
}

/**
 * massert is like uassert but it logs the message before throwing.
 */
#define massert(msgid, msg, expr) \
    MONGO_BASE_ASSERT(::mongo::msgassertedWithLocation, msgid, msg, expr)

#define msgasserted(msgid, msg) \
    MONGO_BASE_ASSERT_FAILED(::mongo::msgassertedWithLocation, msgid, msg)
MONGO_COMPILER_NORETURN void msgassertedWithLocation(const Status& status,
                                                     const char* file,
                                                     unsigned line);

#define massertStatusOK(...) ::mongo::massertStatusOKWithLocation(__VA_ARGS__, __FILE__, __LINE__)
inline void massertStatusOKWithLocation(const Status& status, const char* file, unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        msgassertedWithLocation(status, file, line);
    }
}

/**
 * verify is deprecated. It is like invariant() in debug builds and massert() in release builds.
 */
#define verify(expression) MONGO_verify(expression)
#define MONGO_verify(_Expression)                                    \
    do {                                                             \
        if (MONGO_unlikely(!(_Expression))) {                        \
            ::mongo::verifyFailed(#_Expression, __FILE__, __LINE__); \
        }                                                            \
    } while (false)

inline void invariantWithLocation(const Status& status,
                                  const char* expr,
                                  const char* file,
                                  unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        ::mongo::invariantOKFailed(expr, status, file, line);
    }
}

template <typename T>
inline T invariantWithLocation(StatusWith<T> sw,
                               const char* expr,
                               const char* file,
                               unsigned line) {
    if (MONGO_unlikely(!sw.isOK())) {
        ::mongo::invariantOKFailed(expr, sw.getStatus(), file, line);
    }
    return std::move(sw.getValue());
}

template <typename ContextExpr>
inline void invariantWithContextAndLocation(const Status& status,
                                            const char* expr,
                                            ContextExpr&& contextExpr,
                                            const char* file,
                                            unsigned line) {
    if (MONGO_unlikely(!status.isOK())) {
        ::mongo::invariantOKFailedWithMsg(
            expr, status, std::forward<ContextExpr>(contextExpr)(), file, line);
    }
}

template <typename T, typename ContextExpr>
inline T invariantWithContextAndLocation(StatusWith<T> sw,
                                         const char* expr,
                                         ContextExpr&& contextExpr,
                                         const char* file,
                                         unsigned line) {
    if (MONGO_unlikely(!sw.isOK())) {
        ::mongo::invariantOKFailedWithMsg(expr, sw.getStatus(), contextExpr(), file, line);
    }
    return std::move(sw.getValue());
}

// some special ids that we want to duplicate

// > 10000 asserts
// < 10000 AssertionException

enum { ASSERT_ID_DUPKEY = 11000 };

std::string demangleName(const std::type_info& typeinfo);

/**
 * A utility function that converts an exception to a Status.
 * Only call this function when there is an active exception
 * (e.g. in a catch block).
 *
 * Note: this technique was created by Lisa Lippincott.
 *
 * Example usage:
 *
 *   Status myFunc() {
 *       try {
 *           funcThatThrows();
 *           return Status::OK();
 *       } catch (...) {
 *           return exceptionToStatus();
 *       }
 *   }
 */
Status exceptionToStatus() noexcept;

}  // namespace mongo

#define MONGO_ASSERT_ON_EXCEPTION(expression)                                         \
    try {                                                                             \
        expression;                                                                   \
    } catch (const std::exception& e) {                                               \
        std::stringstream ss;                                                         \
        ss << "caught exception: " << e.what() << ' ' << __FILE__ << ' ' << __LINE__; \
        msgasserted(13294, ss.str());                                                 \
    } catch (...) {                                                                   \
        massert(10437, "unknown exception", false);                                   \
    }

#define MONGO_ASSERT_ON_EXCEPTION_WITH_MSG(expression, msg)         \
    try {                                                           \
        expression;                                                 \
    } catch (const std::exception& e) {                             \
        std::stringstream ss;                                       \
        ss << msg << " caught exception exception: " << e.what();   \
        msgasserted(14043, ss.str());                               \
    } catch (...) {                                                 \
        msgasserted(14044, std::string("unknown exception") + msg); \
    }

/**
 * The purpose of this macro is to instruct the compiler that a line of code will never be reached.
 *
 * Example:
 *     // code above checks that expr can only be FOO or BAR
 *     switch (expr) {
 *     case FOO: { ... }
 *     case BAR: { ... }
 *     default:
 *         MONGO_UNREACHABLE;
 */

#define MONGO_UNREACHABLE ::mongo::invariantFailed("Hit a MONGO_UNREACHABLE!", __FILE__, __LINE__);
