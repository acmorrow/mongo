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

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kResharding

#include "mongo/db/s/resharding/resharding_donor_service.h"

#include <fmt/format.h>

#include "mongo/db/catalog/drop_collection.h"
#include "mongo/db/catalog_raii.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/index_builds_coordinator.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/ops/delete.h"
#include "mongo/db/persistent_task_store.h"
#include "mongo/db/repl/repl_client_info.h"
#include "mongo/db/repl/wait_for_majority_service.h"
#include "mongo/db/s/resharding/resharding_data_copy_util.h"
#include "mongo/db/s/resharding/resharding_metrics.h"
#include "mongo/db/s/resharding/resharding_server_parameters_gen.h"
#include "mongo/db/s/resharding_util.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/logv2/log.h"
#include "mongo/s/catalog/sharding_catalog_client.h"
#include "mongo/s/grid.h"
#include "mongo/util/future_util.h"

namespace mongo {

MONGO_FAIL_POINT_DEFINE(reshardingDonorFailsBeforePreparingToMirror);
MONGO_FAIL_POINT_DEFINE(removeDonorDocFailpoint);

using namespace fmt::literals;

namespace {

const WriteConcernOptions kNoWaitWriteConcern{1, WriteConcernOptions::SyncMode::UNSET, Seconds(0)};

Timestamp generateMinFetchTimestamp(const NamespaceString& sourceNss) {
    auto opCtx = cc().makeOperationContext();

    // Do a no-op write and use the OpTime as the minFetchTimestamp
    writeConflictRetry(
        opCtx.get(),
        "resharding donor minFetchTimestamp",
        NamespaceString::kRsOplogNamespace.ns(),
        [&] {
            AutoGetDb db(opCtx.get(), sourceNss.db(), MODE_IX);
            Lock::CollectionLock collLock(opCtx.get(), sourceNss, MODE_S);

            AutoGetOplog oplogWrite(opCtx.get(), OplogAccessMode::kWrite);

            const std::string msg = str::stream()
                << "All future oplog entries on the namespace " << sourceNss.ns()
                << " must include a 'destinedRecipient' field";
            WriteUnitOfWork wuow(opCtx.get());
            opCtx->getClient()->getServiceContext()->getOpObserver()->onInternalOpMessage(
                opCtx.get(),
                NamespaceString::kForceOplogBatchBoundaryNamespace,
                boost::none,
                BSON("msg" << msg),
                boost::none,
                boost::none,
                boost::none,
                boost::none,
                boost::none);
            wuow.commit();
        });

    auto generatedOpTime = repl::ReplClientInfo::forClient(opCtx->getClient()).getLastOp();
    return generatedOpTime.getTimestamp();
}

/**
 * Returns whether it is possible for the donor to be in 'state' when resharding will indefinitely
 * abort.
 */
bool inPotentialAbortScenario(const DonorStateEnum& state) {
    // Regardless of whether resharding will abort or commit, the donor will eventually reach state
    // kDone.
    // Additionally, if the donor is in state kError, it is guaranteed that the coordinator will
    // eventually begin the abort process.
    return state == DonorStateEnum::kError || state == DonorStateEnum::kDone;
}

/**
 * Fulfills the promise if it is not already. Otherwise, does nothing.
 */
void ensureFulfilledPromise(WithLock lk, SharedPromise<void>& sp) {
    if (!sp.getFuture().isReady()) {
        sp.emplaceValue();
    }
}

void ensureFulfilledPromise(WithLock lk, SharedPromise<void>& sp, Status error) {
    if (!sp.getFuture().isReady()) {
        sp.setError(error);
    }
}

/**
 * Automatically retries the callable until there is an error encountered that resharding cannot
 * recover from or the cancelToken is canceled.
 */
template <typename Callable>
ExecutorFuture<void> withAutomaticRetry(std::shared_ptr<executor::TaskExecutor> executor,
                                        CancellationToken cancelToken,
                                        Callable&& callable) {
    return AsyncTry<Callable>(std::move(callable))
        .until([cancelToken](Status status) {
            if (status.isA<ErrorCategory::RetriableError>() ||
                status.isA<ErrorCategory::CursorInvalidatedError>() ||
                status == ErrorCodes::Interrupted ||
                status.isA<ErrorCategory::CancellationError>() ||
                status.isA<ErrorCategory::NotPrimaryError>()) {
                // Retry on errors from stray killCursors and killOp commands being run. Also retry
                // for notPrimary and cancellation errors to ensure the loop is not prematurely
                // canceled if the errors originate from a remote shard instead of this shard - if
                // there is a failover/stepdown, the cancelToken will eventually be canceled and
                // bypass this .until() block altogether.
                return false;
            }

            return true;
        })
        .on(std::move(executor), cancelToken);
}

class ExternalStateImpl : public ReshardingDonorService::DonorStateMachineExternalState {
public:
    ShardId myShardId(ServiceContext* serviceContext) const override {
        return ShardingState::get(serviceContext)->shardId();
    }

    void refreshCatalogCache(OperationContext* opCtx, const NamespaceString& nss) override {
        auto catalogCache = Grid::get(opCtx)->catalogCache();
        uassertStatusOK(catalogCache->getShardedCollectionRoutingInfoWithRefresh(opCtx, nss));
    }

    void waitForCollectionFlush(OperationContext* opCtx, const NamespaceString& nss) override {
        CatalogCacheLoader::get(opCtx).waitForCollectionFlush(opCtx, nss);
    }

    void updateCoordinatorDocument(OperationContext* opCtx,
                                   const BSONObj& query,
                                   const BSONObj& update) override {
        auto catalogClient = Grid::get(opCtx)->catalogClient();
        uassertStatusOK(catalogClient->updateConfigDocument(
            opCtx,
            NamespaceString::kConfigReshardingOperationsNamespace,
            query,
            update,
            false, /* upsert */
            ShardingCatalogClient::kMajorityWriteConcern));
    }
};

}  // namespace

std::shared_ptr<repl::PrimaryOnlyService::Instance> ReshardingDonorService::constructInstance(
    BSONObj initialState) {
    return std::make_shared<DonorStateMachine>(std::move(initialState),
                                               std::make_unique<ExternalStateImpl>());
}

ReshardingDonorService::DonorStateMachine::DonorStateMachine(
    const BSONObj& donorDoc, std::unique_ptr<DonorStateMachineExternalState> externalState)
    : DonorStateMachine(ReshardingDonorDocument::parse({"DonorStateMachine"}, donorDoc),
                        std::move(externalState)) {}

ReshardingDonorService::DonorStateMachine::DonorStateMachine(
    const ReshardingDonorDocument& donorDoc,
    std::unique_ptr<DonorStateMachineExternalState> externalState)
    : repl::PrimaryOnlyService::TypedInstance<DonorStateMachine>(),
      _metadata{donorDoc.getCommonReshardingMetadata()},
      _recipientShardIds{donorDoc.getRecipientShards()},
      _donorCtx{donorDoc.getMutableState()},
      _externalState{std::move(externalState)} {
    invariant(_externalState);
}

ReshardingDonorService::DonorStateMachine::~DonorStateMachine() {
    stdx::lock_guard<Latch> lg(_mutex);
    invariant(_completionPromise.getFuture().isReady());
}

ExecutorFuture<void> ReshardingDonorService::DonorStateMachine::_runUntilBlockingWritesOrErrored(
    const std::shared_ptr<executor::ScopedTaskExecutor>& executor,
    const CancellationToken& abortToken) noexcept {
    return withAutomaticRetry(
               **executor,
               abortToken,
               [this, executor, abortToken] {
                   return ExecutorFuture(**executor)
                       .then([this] {
                           _onPreparingToDonateCalculateTimestampThenTransitionToDonatingInitialData();
                       })
                       .then([this, executor, abortToken] {
                           return _awaitAllRecipientsDoneCloningThenTransitionToDonatingOplogEntries(
                               executor, abortToken);
                       })
                       .then([this, executor, abortToken] {
                           return _awaitAllRecipientsDoneApplyingThenTransitionToPreparingToBlockWrites(
                               executor, abortToken);
                       })
                       .then(
                           [this] { _writeTransactionOplogEntryThenTransitionToBlockingWrites(); });
               })
        .onError([this, executor, abortToken](Status status) {
            if (abortToken.isCanceled()) {
                return ExecutorFuture<void>(**executor, status);
            }

            LOGV2(4956400,
                  "Resharding operation donor state machine failed",
                  "namespace"_attr = _metadata.getSourceNss(),
                  "reshardingUUID"_attr = _metadata.getReshardingUUID(),
                  "error"_attr = status);

            return withAutomaticRetry(**executor, abortToken, [this, status] {
                // It is illegal to transition into kError if state has already surpassed
                // kPreparingToBlockWrites.
                invariant(_donorCtx.getState() < DonorStateEnum::kBlockingWrites);
                _transitionToError(status);

                // Intentionally swallow the error - by transitioning to kError, the donor
                // effectively recovers from encountering the error and should continue running in
                // the future chain.
            });
        });
}

ExecutorFuture<void> ReshardingDonorService::DonorStateMachine::_notifyCoordinatorAndAwaitDecision(
    const std::shared_ptr<executor::ScopedTaskExecutor>& executor,
    const CancellationToken& abortToken) noexcept {
    if (_donorCtx.getState() == DonorStateEnum::kDone) {
        return ExecutorFuture(**executor);
    }

    return withAutomaticRetry(**executor,
                              abortToken,
                              [this, executor] {
                                  auto opCtx = cc().makeOperationContext();
                                  return _updateCoordinator(opCtx.get(), executor);
                              })
        .then([this, abortToken] {
            return future_util::withCancellation(_coordinatorHasDecisionPersisted.getFuture(),
                                                 abortToken);
        });
}

ExecutorFuture<void> ReshardingDonorService::DonorStateMachine::_finishReshardingOperation(
    const std::shared_ptr<executor::ScopedTaskExecutor>& executor,
    const CancellationToken& stepdownToken,
    bool aborted) noexcept {
    return withAutomaticRetry(**executor, stepdownToken, [this, executor, stepdownToken, aborted] {
        if (!aborted) {
            // If a failover occured after the donor transitioned to done locally, but before it
            // notified the coordinator, it will already be in state done here. Otherwise, it must
            // be in blocking-writes before transitioning to done.
            invariant(_donorCtx.getState() == DonorStateEnum::kBlockingWrites ||
                      _donorCtx.getState() == DonorStateEnum::kDone);

            _dropOriginalCollectionThenTransitionToDone();
        } else if (_donorCtx.getState() != DonorStateEnum::kDone) {
            // If aborted, the donor must be allowed to transition to done from any state.
            _transitionState(DonorStateEnum::kDone);
        }

        auto opCtx = cc().makeOperationContext();
        return _updateCoordinator(opCtx.get(), executor).then([this] {
            {
                auto opCtx = cc().makeOperationContext();
                removeDonorDocFailpoint.pauseWhileSet(opCtx.get());
            }
            _removeDonorDocument();
        });
    });
}

SemiFuture<void> ReshardingDonorService::DonorStateMachine::run(
    std::shared_ptr<executor::ScopedTaskExecutor> executor,
    const CancellationToken& stepdownToken) noexcept {
    auto abortToken = _initAbortSource(stepdownToken);

    return _runUntilBlockingWritesOrErrored(executor, abortToken)
        .then([this, executor, abortToken] {
            return _notifyCoordinatorAndAwaitDecision(executor, abortToken);
        })
        .onCompletion([executor, stepdownToken, abortToken](Status status) {
            if (stepdownToken.isCanceled()) {
                // Propagate any errors from the donor stepping down.
                return ExecutorFuture<bool>(**executor, status);
            }

            if (!status.isOK() && !abortToken.isCanceled()) {
                // Propagate any errors from the donor failing to notify the coordinator.
                return ExecutorFuture<bool>(**executor, status);
            }

            return ExecutorFuture(**executor, abortToken.isCanceled());
        })
        .then([this, executor, stepdownToken](bool aborted) {
            return _finishReshardingOperation(executor, stepdownToken, aborted);
        })
        .onError([this, stepdownToken](Status status) {
            if (stepdownToken.isCanceled()) {
                // The operation will continue on a new DonorStateMachine.
                return status;
            }

            LOGV2_FATAL(5160600,
                        "Unrecoverable error occurred past the point donor was prepared to "
                        "complete the resharding operation",
                        "error"_attr = redact(status));
        })
        // The shared_ptr stored in the PrimaryOnlyService's map for the ReshardingDonorService
        // Instance is removed when the donor state document tied to the instance is deleted. It is
        // necessary to use shared_from_this() to extend the lifetime so the all earlier code can
        // safely finish executing.
        .onCompletion([anchor = shared_from_this()](Status status) { return status; })
        .semi();
}

void ReshardingDonorService::DonorStateMachine::interrupt(Status status) {
    stdx::lock_guard<Latch> lk(_mutex);
    if (!_completionPromise.getFuture().isReady()) {
        _completionPromise.setError(status);
    }
}

boost::optional<BSONObj> ReshardingDonorService::DonorStateMachine::reportForCurrentOp(
    MongoProcessInterface::CurrentOpConnectionsMode connMode,
    MongoProcessInterface::CurrentOpSessionsMode sessionMode) noexcept {
    ReshardingMetrics::ReporterOptions options(ReshardingMetrics::ReporterOptions::Role::kDonor,
                                               _metadata.getReshardingUUID(),
                                               _metadata.getSourceNss(),
                                               _metadata.getReshardingKey().toBSON(),
                                               false);
    return ReshardingMetrics::get(cc().getServiceContext())->reportForCurrentOp(options);
}

void ReshardingDonorService::DonorStateMachine::onReshardingFieldsChanges(
    OperationContext* opCtx, const TypeCollectionReshardingFields& reshardingFields) {
    if (reshardingFields.getAbortReason()) {
        auto abortReason = getStatusFromAbortReason(reshardingFields);
        _onAbortEncountered(abortReason);
        return;
    }

    stdx::lock_guard<Latch> lk(_mutex);
    auto coordinatorState = reshardingFields.getState();
    if (coordinatorState >= CoordinatorStateEnum::kApplying) {
        ensureFulfilledPromise(lk, _allRecipientsDoneCloning);
    }

    if (coordinatorState >= CoordinatorStateEnum::kBlockingWrites) {
        _critSec.emplace(opCtx->getServiceContext(), _metadata.getSourceNss());

        ensureFulfilledPromise(lk, _allRecipientsDoneApplying);
    }

    if (coordinatorState >= CoordinatorStateEnum::kDecisionPersisted) {
        ensureFulfilledPromise(lk, _coordinatorHasDecisionPersisted);
    }

    if (coordinatorState >= CoordinatorStateEnum::kDone) {
        _critSec.reset();
    }
}

void ReshardingDonorService::DonorStateMachine::
    _onPreparingToDonateCalculateTimestampThenTransitionToDonatingInitialData() {
    if (_donorCtx.getState() > DonorStateEnum::kPreparingToDonate) {
        if (!inPotentialAbortScenario(_donorCtx.getState())) {
            // The invariants won't hold if an unrecoverable error is encountered before the donor
            // makes enough progress to transition to kDonatingInitialData and then a failover
            // occurs.
            invariant(_donorCtx.getMinFetchTimestamp());
            invariant(_donorCtx.getBytesToClone());
            invariant(_donorCtx.getDocumentsToClone());
        }
        return;
    }

    int64_t bytesToClone = 0;
    int64_t documentsToClone = 0;

    {
        auto opCtx = cc().makeOperationContext();
        auto rawOpCtx = opCtx.get();

        AutoGetCollection coll(rawOpCtx, _metadata.getSourceNss(), MODE_IS);
        if (coll) {
            IndexBuildsCoordinator::get(rawOpCtx)->assertNoIndexBuildInProgForCollection(
                coll->uuid());

            bytesToClone = coll->dataSize(rawOpCtx);
            documentsToClone = coll->numRecords(rawOpCtx);
        }
    }

    // Recipient shards expect to read from the donor shard's existing sharded collection and the
    // config.cache.chunks collection of the temporary resharding collection using
    // {atClusterTime: <fetchTimestamp>}. Refreshing the temporary resharding collection on the
    // donor shards causes them to create the config.cache.chunks collection. Without this refresh,
    // the {atClusterTime: <fetchTimestamp>} read on the config.cache.chunks namespace would fail
    // with a SnapshotUnavailable error response.
    {
        auto opCtx = cc().makeOperationContext();
        _externalState->refreshCatalogCache(opCtx.get(), _metadata.getTempReshardingNss());
        _externalState->waitForCollectionFlush(opCtx.get(), _metadata.getTempReshardingNss());
    }

    Timestamp minFetchTimestamp = generateMinFetchTimestamp(_metadata.getSourceNss());

    LOGV2_DEBUG(5390702,
                2,
                "Collection being resharded now ready for recipients to begin cloning",
                "namespace"_attr = _metadata.getSourceNss(),
                "minFetchTimestamp"_attr = minFetchTimestamp,
                "bytesToClone"_attr = bytesToClone,
                "documentsToClone"_attr = documentsToClone,
                "reshardingUUID"_attr = _metadata.getReshardingUUID());

    _transitionToDonatingInitialData(minFetchTimestamp, bytesToClone, documentsToClone);
}

ExecutorFuture<void> ReshardingDonorService::DonorStateMachine::
    _awaitAllRecipientsDoneCloningThenTransitionToDonatingOplogEntries(
        const std::shared_ptr<executor::ScopedTaskExecutor>& executor,
        const CancellationToken& abortToken) {
    if (_donorCtx.getState() > DonorStateEnum::kDonatingInitialData) {
        return ExecutorFuture<void>(**executor, Status::OK());
    }

    auto opCtx = cc().makeOperationContext();
    return _updateCoordinator(opCtx.get(), executor)
        .then([this, abortToken] {
            return future_util::withCancellation(_allRecipientsDoneCloning.getFuture(), abortToken);
        })
        .thenRunOn(**executor)
        .then([this]() { _transitionState(DonorStateEnum::kDonatingOplogEntries); })
        .onCompletion([=](Status s) {
            if (MONGO_unlikely(reshardingDonorFailsBeforePreparingToMirror.shouldFail())) {
                uasserted(ErrorCodes::InternalError, "Failing for test");
            }
        });
}

ExecutorFuture<void> ReshardingDonorService::DonorStateMachine::
    _awaitAllRecipientsDoneApplyingThenTransitionToPreparingToBlockWrites(
        const std::shared_ptr<executor::ScopedTaskExecutor>& executor,
        const CancellationToken& abortToken) {
    if (_donorCtx.getState() > DonorStateEnum::kDonatingOplogEntries) {
        return ExecutorFuture<void>(**executor, Status::OK());
    }

    return future_util::withCancellation(_allRecipientsDoneApplying.getFuture(), abortToken)
        .thenRunOn(**executor)
        .then([this]() { _transitionState(DonorStateEnum::kPreparingToBlockWrites); });
}

void ReshardingDonorService::DonorStateMachine::
    _writeTransactionOplogEntryThenTransitionToBlockingWrites() {
    if (_donorCtx.getState() > DonorStateEnum::kPreparingToBlockWrites) {
        return;
    }

    {
        auto opCtx = cc().makeOperationContext();
        auto rawOpCtx = opCtx.get();

        auto generateOplogEntry = [&](ShardId destinedRecipient) {
            repl::MutableOplogEntry oplog;
            oplog.setNss(_metadata.getSourceNss());
            oplog.setOpType(repl::OpTypeEnum::kNoop);
            oplog.setUuid(_metadata.getSourceUUID());
            oplog.setDestinedRecipient(destinedRecipient);
            oplog.setObject(
                BSON("msg" << fmt::format("Writes to {} are temporarily blocked for resharding.",
                                          _metadata.getSourceNss().toString())));
            oplog.setObject2(BSON("type" << kReshardFinalOpLogType << "reshardingUUID"
                                         << _metadata.getReshardingUUID()));
            oplog.setOpTime(OplogSlot());
            oplog.setWallClockTime(opCtx->getServiceContext()->getFastClockSource()->now());
            return oplog;
        };

        try {
            Timer latency;

            for (const auto& recipient : _recipientShardIds) {
                auto oplog = generateOplogEntry(recipient);
                writeConflictRetry(
                    rawOpCtx,
                    "ReshardingBlockWritesOplog",
                    NamespaceString::kRsOplogNamespace.ns(),
                    [&] {
                        AutoGetOplog oplogWrite(rawOpCtx, OplogAccessMode::kWrite);
                        WriteUnitOfWork wunit(rawOpCtx);
                        const auto& oplogOpTime = repl::logOp(rawOpCtx, &oplog);
                        uassert(5279507,
                                str::stream()
                                    << "Failed to create new oplog entry for oplog with opTime: "
                                    << oplog.getOpTime().toString() << ": "
                                    << redact(oplog.toBSON()),
                                !oplogOpTime.isNull());
                        wunit.commit();
                    });
            }

            {
                stdx::lock_guard<Latch> lg(_mutex);
                LOGV2_DEBUG(5279504,
                            0,
                            "Committed oplog entries to temporarily block writes for resharding",
                            "namespace"_attr = _metadata.getSourceNss(),
                            "reshardingUUID"_attr = _metadata.getReshardingUUID(),
                            "numRecipients"_attr = _recipientShardIds.size(),
                            "duration"_attr = duration_cast<Milliseconds>(latency.elapsed()));
                ensureFulfilledPromise(lg, _finalOplogEntriesWritten);
            }
        } catch (const DBException& e) {
            const auto& status = e.toStatus();
            stdx::lock_guard<Latch> lg(_mutex);
            LOGV2_ERROR(5279508,
                        "Exception while writing resharding final oplog entries",
                        "reshardingUUID"_attr = _metadata.getReshardingUUID(),
                        "error"_attr = status);
            ensureFulfilledPromise(lg, _finalOplogEntriesWritten, status);
            uassertStatusOK(status);
        }
    }

    _transitionState(DonorStateEnum::kBlockingWrites);
}

SharedSemiFuture<void> ReshardingDonorService::DonorStateMachine::awaitFinalOplogEntriesWritten() {
    return _finalOplogEntriesWritten.getFuture();
}

void ReshardingDonorService::DonorStateMachine::_dropOriginalCollectionThenTransitionToDone() {
    if (_donorCtx.getState() > DonorStateEnum::kBlockingWrites) {
        return;
    }

    {
        auto opCtx = cc().makeOperationContext();
        resharding::data_copy::ensureCollectionDropped(
            opCtx.get(), _metadata.getSourceNss(), _metadata.getSourceUUID());
    }

    _transitionState(DonorStateEnum::kDone);
}

void ReshardingDonorService::DonorStateMachine::_transitionState(DonorStateEnum newState) {
    invariant(newState != DonorStateEnum::kDonatingInitialData &&
              newState != DonorStateEnum::kError);

    auto newDonorCtx = _donorCtx;
    newDonorCtx.setState(newState);
    _transitionState(std::move(newDonorCtx));
}

void ReshardingDonorService::DonorStateMachine::_transitionState(DonorShardContext&& newDonorCtx) {
    // For logging purposes.
    auto oldState = _donorCtx.getState();
    auto newState = newDonorCtx.getState();

    _updateDonorDocument(std::move(newDonorCtx));

    LOGV2_INFO(5279505,
               "Transitioned resharding donor state",
               "newState"_attr = DonorState_serializer(newState),
               "oldState"_attr = DonorState_serializer(oldState),
               "namespace"_attr = _metadata.getSourceNss(),
               "collectionUUID"_attr = _metadata.getSourceUUID(),
               "reshardingUUID"_attr = _metadata.getReshardingUUID());
}

void ReshardingDonorService::DonorStateMachine::_transitionToDonatingInitialData(
    Timestamp minFetchTimestamp, int64_t bytesToClone, int64_t documentsToClone) {
    auto newDonorCtx = _donorCtx;
    newDonorCtx.setState(DonorStateEnum::kDonatingInitialData);
    newDonorCtx.setMinFetchTimestamp(minFetchTimestamp);
    newDonorCtx.setBytesToClone(bytesToClone);
    newDonorCtx.setDocumentsToClone(documentsToClone);
    _transitionState(std::move(newDonorCtx));
}

void ReshardingDonorService::DonorStateMachine::_transitionToError(Status abortReason) {
    auto newDonorCtx = _donorCtx;
    newDonorCtx.setState(DonorStateEnum::kError);
    emplaceAbortReasonIfExists(newDonorCtx, abortReason);
    _transitionState(std::move(newDonorCtx));
}

/**
 * Returns a query filter of the form
 * {
 *     _id: <reshardingUUID>,
 *     donorShards: {$elemMatch: {
 *         id: <this donor's ShardId>,
 *         "mutableState.state: {$in: [ <list of valid current states> ]},
 *     }},
 * }
 */
BSONObj ReshardingDonorService::DonorStateMachine::_makeQueryForCoordinatorUpdate(
    const ShardId& shardId, DonorStateEnum newState) {
    // The donor only updates the coordinator when it transitions to states which the coordinator
    // depends on for its own transitions. The table maps the donor states which could be updated on
    // the coordinator to the only states the donor could have already persisted to the current
    // coordinator document in order for its transition to the newState to be valid.
    static const stdx::unordered_map<DonorStateEnum, std::vector<DonorStateEnum>>
        validPreviousStateMap = {
            {DonorStateEnum::kDonatingInitialData, {DonorStateEnum::kUnused}},
            {DonorStateEnum::kError,
             {DonorStateEnum::kUnused, DonorStateEnum::kDonatingInitialData}},
            {DonorStateEnum::kBlockingWrites, {DonorStateEnum::kDonatingInitialData}},
            {DonorStateEnum::kDone,
             {DonorStateEnum::kUnused,
              DonorStateEnum::kDonatingInitialData,
              DonorStateEnum::kError,
              DonorStateEnum::kBlockingWrites}},
        };

    auto it = validPreviousStateMap.find(newState);
    invariant(it != validPreviousStateMap.end());

    // The network isn't perfectly reliable so it is possible for update commands sent by
    // _updateCoordinator() to be received out of order by the coordinator. To overcome this
    // behavior, the donor shard includes the list of valid current states as part of the
    // update to transition to the next state. This way, the update from a delayed message
    // won't match the document if it or any later state transitions have already occurred.
    BSONObjBuilder queryBuilder;
    {
        _metadata.getReshardingUUID().appendToBuilder(
            &queryBuilder, ReshardingCoordinatorDocument::kReshardingUUIDFieldName);

        BSONObjBuilder donorShardsBuilder(
            queryBuilder.subobjStart(ReshardingCoordinatorDocument::kDonorShardsFieldName));
        {
            BSONObjBuilder elemMatchBuilder(donorShardsBuilder.subobjStart("$elemMatch"));
            {
                elemMatchBuilder.append(DonorShardEntry::kIdFieldName, shardId);

                BSONObjBuilder mutableStateBuilder(
                    elemMatchBuilder.subobjStart(DonorShardEntry::kMutableStateFieldName + "." +
                                                 DonorShardContext::kStateFieldName));
                {
                    BSONArrayBuilder inBuilder(mutableStateBuilder.subarrayStart("$in"));
                    for (const auto& state : it->second) {
                        inBuilder.append(DonorState_serializer(state));
                    }
                }
            }
        }
    }

    return queryBuilder.obj();
}

ExecutorFuture<void> ReshardingDonorService::DonorStateMachine::_updateCoordinator(
    OperationContext* opCtx, const std::shared_ptr<executor::ScopedTaskExecutor>& executor) {
    repl::ReplClientInfo::forClient(opCtx->getClient()).setLastOpToSystemLastOpTime(opCtx);
    auto clientOpTime = repl::ReplClientInfo::forClient(opCtx->getClient()).getLastOp();
    return WaitForMajorityService::get(opCtx->getServiceContext())
        .waitUntilMajority(clientOpTime, CancellationToken::uncancelable())
        .thenRunOn(**executor)
        .then([this] {
            auto opCtx = cc().makeOperationContext();
            auto shardId = _externalState->myShardId(opCtx->getServiceContext());

            BSONObjBuilder updateBuilder;
            {
                BSONObjBuilder setBuilder(updateBuilder.subobjStart("$set"));
                {
                    setBuilder.append(ReshardingCoordinatorDocument::kDonorShardsFieldName + ".$." +
                                          DonorShardEntry::kMutableStateFieldName,
                                      _donorCtx.toBSON());
                }
            }

            _externalState->updateCoordinatorDocument(
                opCtx.get(),
                _makeQueryForCoordinatorUpdate(shardId, _donorCtx.getState()),
                updateBuilder.done());
        });
}

void ReshardingDonorService::DonorStateMachine::insertStateDocument(
    OperationContext* opCtx, const ReshardingDonorDocument& donorDoc) {
    PersistentTaskStore<ReshardingDonorDocument> store(
        NamespaceString::kDonorReshardingOperationsNamespace);
    store.add(opCtx, donorDoc, kNoWaitWriteConcern);
}

void ReshardingDonorService::DonorStateMachine::_updateDonorDocument(
    DonorShardContext&& newDonorCtx) {
    auto opCtx = cc().makeOperationContext();
    PersistentTaskStore<ReshardingDonorDocument> store(
        NamespaceString::kDonorReshardingOperationsNamespace);
    store.update(
        opCtx.get(),
        BSON(ReshardingDonorDocument::kReshardingUUIDFieldName << _metadata.getReshardingUUID()),
        BSON("$set" << BSON(ReshardingDonorDocument::kMutableStateFieldName
                            << newDonorCtx.toBSON())),
        kNoWaitWriteConcern);

    _donorCtx = newDonorCtx;
}

void ReshardingDonorService::DonorStateMachine::_removeDonorDocument() {
    auto opCtx = cc().makeOperationContext();

    const auto& nss = NamespaceString::kDonorReshardingOperationsNamespace;
    writeConflictRetry(opCtx.get(), "DonorStateMachine::_removeDonorDocument", nss.toString(), [&] {
        AutoGetCollection coll(opCtx.get(), nss, MODE_IX);

        if (!coll) {
            return;
        }

        WriteUnitOfWork wuow(opCtx.get());

        opCtx->recoveryUnit()->onCommit([this](boost::optional<Timestamp> unusedCommitTime) {
            stdx::lock_guard<Latch> lk(_mutex);
            if (_abortReason) {
                ensureFulfilledPromise(lk, _completionPromise, _abortReason.get());
            } else {
                ensureFulfilledPromise(lk, _completionPromise);
            }
        });

        deleteObjects(opCtx.get(),
                      *coll,
                      nss,
                      BSON(ReshardingDonorDocument::kReshardingUUIDFieldName
                           << _metadata.getReshardingUUID()),
                      true /* justOne */);

        wuow.commit();
    });
}

CancellationToken ReshardingDonorService::DonorStateMachine::_initAbortSource(
    const CancellationToken& stepdownToken) {
    {
        stdx::lock_guard<Latch> lk(_mutex);
        _abortSource = CancellationSource(stepdownToken);
    }

    if (auto future = _coordinatorHasDecisionPersisted.getFuture(); future.isReady()) {
        if (auto status = future.getNoThrow(); !status.isOK()) {
            // onReshardingFieldsChanges() missed canceling _abortSource because _initAbortSource()
            // hadn't been called yet. We used an error status stored in
            // _coordinatorHasDecisionPersisted as an indication that an abort had been received.
            // Canceling _abortSource immediately allows callers to use the returned abortToken as a
            // definitive means of checking whether the operation has been aborted.
            _abortSource->cancel();
        }
    }

    return _abortSource->token();
}

void ReshardingDonorService::DonorStateMachine::_onAbortEncountered(const Status& abortReason) {
    auto abortSource = [&]() -> boost::optional<CancellationSource> {
        stdx::lock_guard<Latch> lk(_mutex);
        _abortReason = abortReason;
        invariant(!_abortReason->isOK());

        if (_abortSource) {
            return _abortSource;
        } else {
            // run() hasn't been called, notify the operation should be aborted by setting an
            // error.
            invariant(!_coordinatorHasDecisionPersisted.getFuture().isReady());
            _coordinatorHasDecisionPersisted.setError(_abortReason.get());
            return boost::none;
        }
    }();

    if (abortSource) {
        abortSource->cancel();
    }

    _critSec.reset();
}

}  // namespace mongo
