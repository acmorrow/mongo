/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include "mongo/platform/basic.h"

#include "mongo/db/catalog_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/dbhelpers.h"
#include "mongo/db/repl/apply_ops.h"
#include "mongo/db/repl/oplog_interface_local.h"
#include "mongo/db/repl/wait_for_majority_service.h"
#include "mongo/db/s/collection_sharding_runtime.h"
#include "mongo/db/s/operation_sharding_state.h"
#include "mongo/db/s/resharding_util.h"
#include "mongo/db/s/shard_filtering_metadata_refresh.h"
#include "mongo/db/s/shard_server_test_fixture.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/db/session_catalog_mongod.h"
#include "mongo/db/transaction_participant.h"
#include "mongo/s/catalog/sharding_catalog_client_mock.h"
#include "mongo/s/catalog/type_shard.h"
#include "mongo/s/catalog_cache_loader_mock.h"
#include "mongo/s/database_version_helpers.h"
#include "mongo/s/shard_id.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

template <typename Callable>
void runInTransaction(OperationContext* opCtx, Callable&& func) {
    auto sessionId = makeLogicalSessionIdForTest();
    const TxnNumber txnNum = 0;

    opCtx->setLogicalSessionId(sessionId);
    opCtx->setTxnNumber(txnNum);
    opCtx->setInMultiDocumentTransaction();

    MongoDOperationContextSession ocs(opCtx);

    auto txnParticipant = TransactionParticipant::get(opCtx);
    ASSERT(txnParticipant);
    txnParticipant.beginOrContinue(opCtx, txnNum, false, true);
    txnParticipant.unstashTransactionResources(opCtx, "SetDestinedRecipient");

    func();

    txnParticipant.commitUnpreparedTransaction(opCtx);
    txnParticipant.stashTransactionResources(opCtx);
}

class DestinedRecipientTest : public ShardServerTestFixture {
public:
    const NamespaceString kNss{"test.foo"};
    const std::string kShardKey = "x";
    const HostAndPort kConfigHostAndPort{"DummyConfig", 12345};
    const std::vector<ShardType> kShardList = {ShardType("shard0", "Host0:12345"),
                                               ShardType("shard1", "Host1:12345")};

    void setUp() override {
        // Don't call ShardServerTestFixture::setUp so we can install a mock catalog cache loader.
        ShardingMongodTestFixture::setUp();

        replicationCoordinator()->alwaysAllowWrites(true);
        serverGlobalParams.clusterRole = ClusterRole::ShardServer;

        _clusterId = OID::gen();
        ShardingState::get(getServiceContext())
            ->setInitialized(kShardList[0].getName(), _clusterId);

        auto mockLoader = std::make_unique<CatalogCacheLoaderMock>();
        _mockCatalogCacheLoader = mockLoader.get();
        CatalogCacheLoader::set(getServiceContext(), std::move(mockLoader));

        uassertStatusOK(
            initializeGlobalShardingStateForMongodForTest(ConnectionString(kConfigHostAndPort)));

        configTargeterMock()->setFindHostReturnValue(kConfigHostAndPort);

        WaitForMajorityService::get(getServiceContext()).setUp(getServiceContext());

        for (const auto& shard : kShardList) {
            std::unique_ptr<RemoteCommandTargeterMock> targeter(
                std::make_unique<RemoteCommandTargeterMock>());
            HostAndPort host(shard.getHost());
            targeter->setConnectionStringReturnValue(ConnectionString(host));
            targeter->setFindHostReturnValue(host);
            targeterFactory()->addTargeterToReturn(ConnectionString(host), std::move(targeter));
        }
    }

    void tearDown() override {
        WaitForMajorityService::get(getServiceContext()).shutDown();

        ShardServerTestFixture::tearDown();
    }

    class StaticCatalogClient final : public ShardingCatalogClientMock {
    public:
        StaticCatalogClient(std::vector<ShardType> shards)
            : ShardingCatalogClientMock(nullptr), _shards(std::move(shards)) {}

        StatusWith<repl::OpTimeWith<std::vector<ShardType>>> getAllShards(
            OperationContext* opCtx, repl::ReadConcernLevel readConcern) override {
            return repl::OpTimeWith<std::vector<ShardType>>(_shards);
        }

        StatusWith<std::vector<CollectionType>> getCollections(
            OperationContext* opCtx,
            const std::string* dbName,
            repl::OpTime* optime,
            repl::ReadConcernLevel readConcernLevel) override {
            return _colls;
        }

        void setCollections(std::vector<CollectionType> colls) {
            _colls = std::move(colls);
        }

    private:
        const std::vector<ShardType> _shards;
        std::vector<CollectionType> _colls;
    };

    std::unique_ptr<ShardingCatalogClient> makeShardingCatalogClient(
        std::unique_ptr<DistLockManager> distLockManager) override {
        return std::make_unique<StaticCatalogClient>(kShardList);
    }

protected:
    CollectionType createCollection(const OID& epoch) {
        CollectionType coll;

        coll.setNs(kNss);
        coll.setEpoch(epoch);
        coll.setKeyPattern(BSON(kShardKey << 1));
        coll.setUnique(false);
        coll.setUUID(UUID::gen());

        return coll;
    }

    std::vector<ChunkType> createChunks(const OID& epoch, const std::string& shardKey) {
        auto range1 = ChunkRange(BSON(shardKey << MINKEY), BSON(shardKey << 5));
        ChunkType chunk1(kNss, range1, ChunkVersion(1, 0, epoch), kShardList[0].getName());

        auto range2 = ChunkRange(BSON(shardKey << 5), BSON(shardKey << MAXKEY));
        ChunkType chunk2(kNss, range2, ChunkVersion(1, 0, epoch), kShardList[1].getName());

        return {chunk1, chunk2};
    }

    struct ReshardingEnv {
        ReshardingEnv(UUID uuid) : sourceUuid(std::move(uuid)) {}

        NamespaceString tempNss;
        UUID sourceUuid;
        ShardId destShard;
        ChunkVersion version;
        DatabaseVersion dbVersion;
    };

    ReshardingEnv setupReshardingEnv(OperationContext* opCtx, bool refreshTempNss) {
        DBDirectClient client(opCtx);
        client.createCollection(kNss.ns());
        client.createCollection(NamespaceString::kSessionTransactionsTableNamespace.ns());

        ReshardingEnv env(CollectionCatalog::get(opCtx).lookupUUIDByNSS(opCtx, kNss).value());
        env.destShard = kShardList[1].getName();
        env.version = ChunkVersion(1, 0, OID::gen());
        env.dbVersion = databaseVersion::makeNew();

        env.tempNss =
            NamespaceString(kNss.db(),
                            fmt::format("{}{}",
                                        NamespaceString::kTemporaryReshardingCollectionPrefix,
                                        env.sourceUuid.toString()));

        client.createCollection(env.tempNss.ns());


        DatabaseType db(kNss.db().toString(), kShardList[0].getName(), true, env.dbVersion);

        TypeCollectionReshardingFields reshardingFields;
        reshardingFields.setUuid(UUID::gen());
        reshardingFields.setDonorFields(TypeCollectionDonorFields{BSON("y" << 1)});

        auto collType = createCollection(env.version.epoch());

        _mockCatalogCacheLoader->setDatabaseRefreshReturnValue(db);
        _mockCatalogCacheLoader->setCollectionRefreshValues(
            kNss, collType, createChunks(env.version.epoch(), kShardKey), reshardingFields);
        _mockCatalogCacheLoader->setCollectionRefreshValues(
            env.tempNss, collType, createChunks(env.version.epoch(), "y"), boost::none);

        forceShardFilteringMetadataRefresh(opCtx, kNss);

        if (refreshTempNss)
            forceShardFilteringMetadataRefresh(opCtx, env.tempNss);

        return env;
    }

    void writeDoc(OperationContext* opCtx,
                  const NamespaceString& nss,
                  const BSONObj& doc,
                  const ReshardingEnv& env) {
        WriteUnitOfWork wuow(opCtx);
        AutoGetCollection autoColl1(opCtx, nss, MODE_IX);

        // TODO(SERVER-50027): This is to temporarily make this test pass until getOwnershipFilter
        // has been updated to detect frozen migrations.
        if (!OperationShardingState::isOperationVersioned(opCtx)) {
            OperationShardingState::get(opCtx).initializeClientRoutingVersions(
                nss, env.version, env.dbVersion);
        }

        auto collection = CollectionCatalog::get(opCtx).lookupCollectionByNamespace(opCtx, nss);
        ASSERT(collection);
        auto status = collection->insertDocument(opCtx, InsertStatement(doc), nullptr);
        ASSERT_OK(status);

        wuow.commit();
    }

    void updateDoc(OperationContext* opCtx,
                   const NamespaceString& nss,
                   const BSONObj& filter,
                   const BSONObj& update,
                   const ReshardingEnv& env) {
        AutoGetCollection coll(opCtx, nss, MODE_IX);

        // TODO(SERVER-50027): This is to temporarily make this test pass until getOwnershipFilter
        // has been updated to detect frozen migrations.
        if (!OperationShardingState::isOperationVersioned(opCtx)) {
            OperationShardingState::get(opCtx).initializeClientRoutingVersions(
                kNss, env.version, env.dbVersion);
        }

        Helpers::update(opCtx, nss.toString(), filter, update);
    }

    repl::OplogEntry getLastOplogEntry(OperationContext* opCtx) {
        repl::OplogInterfaceLocal oplogInterface(opCtx);
        auto oplogIter = oplogInterface.makeIterator();

        const auto& doc = unittest::assertGet(oplogIter->next()).first;
        return unittest::assertGet(repl::OplogEntry::parse(doc));
    }

protected:
    CatalogCacheLoaderMock* _mockCatalogCacheLoader;
};

TEST_F(DestinedRecipientTest, TestGetDestinedRecipient) {
    auto opCtx = operationContext();
    auto env = setupReshardingEnv(opCtx, true);

    AutoGetCollection coll(opCtx, kNss, MODE_IX);

    // TODO(SERVER-50027): This is to temporarily make this test pass until getOwnershipFilter has
    // been updated to detect frozen migrations.
    if (!OperationShardingState::isOperationVersioned(opCtx)) {
        OperationShardingState::get(opCtx).initializeClientRoutingVersions(
            kNss, env.version, env.dbVersion);
    }

    auto destShardId = getDestinedRecipient(opCtx, kNss, BSON("x" << 2 << "y" << 10));
    ASSERT(destShardId);
    ASSERT_EQ(*destShardId, env.destShard);
}

TEST_F(DestinedRecipientTest, TestGetDestinedRecipientThrowsOnBlockedRefresh) {
    auto opCtx = operationContext();
    auto env = setupReshardingEnv(opCtx, false);

    {
        AutoGetCollection coll(opCtx, kNss, MODE_IX);

        // TODO(SERVER-50027): This is to temporarily make this test pass until getOwnershipFilter
        // has been updated to detect frozen migrations.
        if (!OperationShardingState::isOperationVersioned(opCtx)) {
            OperationShardingState::get(opCtx).initializeClientRoutingVersions(
                kNss, env.version, env.dbVersion);
        }

        ASSERT_THROWS(getDestinedRecipient(opCtx, kNss, BSON("x" << 2 << "y" << 10)),
                      ExceptionFor<ErrorCodes::ShardInvalidatedForTargeting>);
    }

    auto sw = catalogCache()->getCollectionRoutingInfoWithRefresh(opCtx, env.tempNss);
}

TEST_F(DestinedRecipientTest, TestOpObserverSetsDestinedRecipientOnInserts) {
    auto opCtx = operationContext();
    auto env = setupReshardingEnv(opCtx, true);

    writeDoc(opCtx, kNss, BSON("_id" << 0 << "x" << 2 << "y" << 10), env);

    auto entry = getLastOplogEntry(opCtx);
    auto recipShard = entry.getDestinedRecipient();

    ASSERT(recipShard);
    ASSERT_EQ(*recipShard, env.destShard);
}

TEST_F(DestinedRecipientTest, TestOpObserverSetsDestinedRecipientOnInsertsInTransaction) {
    auto opCtx = operationContext();
    auto env = setupReshardingEnv(opCtx, true);

    runInTransaction(
        opCtx, [&]() { writeDoc(opCtx, kNss, BSON("_id" << 0 << "x" << 2 << "y" << 10), env); });

    // Look for destined recipient in latest oplog entry. Since this write was done in a
    // transaction, the write operation will be embedded in an applyOps entry and needs to be
    // extracted.
    auto entry = getLastOplogEntry(opCtx);
    auto info = repl::ApplyOpsCommandInfo::parse(entry.getOperationToApply());

    auto ops = info.getOperations();
    auto replOp = repl::ReplOperation::parse(IDLParserErrorContext("insertOp"), ops[0]);
    ASSERT_EQ(replOp.getNss(), kNss);

    auto recipShard = replOp.getDestinedRecipient();
    ASSERT(recipShard);
    ASSERT_EQ(*recipShard, env.destShard);
}

TEST_F(DestinedRecipientTest, TestOpObserverSetsDestinedRecipientOnUpdates) {
    auto opCtx = operationContext();

    DBDirectClient client(opCtx);
    client.insert(kNss.toString(), BSON("_id" << 0 << "x" << 2 << "y" << 10 << "z" << 4));

    auto env = setupReshardingEnv(opCtx, true);

    updateDoc(opCtx, kNss, BSON("_id" << 0), BSON("$set" << BSON("z" << 50)), env);

    auto entry = getLastOplogEntry(opCtx);
    auto recipShard = entry.getDestinedRecipient();

    ASSERT(recipShard);
    ASSERT_EQ(*recipShard, env.destShard);
}

TEST_F(DestinedRecipientTest, TestOpObserverSetsDestinedRecipientOnUpdatesOutOfPlace) {
    auto opCtx = operationContext();

    DBDirectClient client(opCtx);
    client.insert(kNss.toString(), BSON("_id" << 0 << "x" << 2 << "y" << 10));

    auto env = setupReshardingEnv(opCtx, true);

    updateDoc(opCtx, kNss, BSON("_id" << 0), BSON("$set" << BSON("z" << 50)), env);

    auto entry = getLastOplogEntry(opCtx);
    auto recipShard = entry.getDestinedRecipient();

    ASSERT(recipShard);
    ASSERT_EQ(*recipShard, env.destShard);
}

TEST_F(DestinedRecipientTest, TestOpObserverSetsDestinedRecipientOnUpdatesInTransaction) {
    auto opCtx = operationContext();

    DBDirectClient client(opCtx);
    client.insert(kNss.toString(), BSON("_id" << 0 << "x" << 2 << "y" << 10 << "z" << 4));

    auto env = setupReshardingEnv(opCtx, true);

    runInTransaction(opCtx, [&]() {
        updateDoc(opCtx, kNss, BSON("_id" << 0), BSON("$set" << BSON("z" << 50)), env);
    });

    // Look for destined recipient in latest oplog entry. Since this write was done in a
    // transaction, the write operation will be embedded in an applyOps entry and needs to be
    // extracted.
    auto entry = getLastOplogEntry(opCtx);
    auto info = repl::ApplyOpsCommandInfo::parse(entry.getOperationToApply());

    auto ops = info.getOperations();
    auto replOp = repl::ReplOperation::parse(IDLParserErrorContext("insertOp"), ops[0]);
    ASSERT_EQ(replOp.getNss(), kNss);

    auto recipShard = replOp.getDestinedRecipient();
    ASSERT(recipShard);
    ASSERT_EQ(*recipShard, env.destShard);
}

}  // namespace
}  // namespace mongo