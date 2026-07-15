#include "library/autodj/autodjfeature.h"

#include <QMenu>
#include <QSet>
#include <QtDebug>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/aidj/mixingcontext.h"
#include "library/aidj/rulestrackranker.h"
#include "library/autodj/autodjprocessor.h"
#include "library/autodj/dlgautodj.h"
#include "library/dao/trackschema.h"
#include "library/library.h"
#include "library/parser.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/cratestorage.h"
#include "library/treeitem.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_autodjfeature.cpp"
#include "sources/soundsourceproxy.h"
#include "track/bpm.h"
#include "track/keyutils.h"
#include "track/track.h"
#include "util/clipboard.h"
#include "util/defs.h"
#include "util/dnd.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"

namespace {
constexpr int kMaxRetrieveAttempts = 3;
// How many candidate tracks the DB pre-filter returns for the ranker to score.
constexpr int kSmartQueueCandidateLimit = 150;
// Default number of recently-picked tracks Smart Queue avoids repeating.
constexpr int kSmartQueueRecentHistoryDefault = 20;

const QString kAutoDjConfigGroup = QStringLiteral("[Auto DJ]");
const QString kEnableSmartQueue = QStringLiteral("EnableSmartQueue");
const QString kSmartQueueMaxBpmDelta = QStringLiteral("SmartQueueMaxBpmDelta");
const QString kSmartQueueRequireCompatibleKey =
        QStringLiteral("SmartQueueRequireCompatibleKey");
const QString kSmartQueueRecentHistory =
        QStringLiteral("SmartQueueRecentHistory");

int findOrCrateAutoDjPlaylistId(PlaylistDAO& playlistDAO) {
    int playlistId = playlistDAO.getPlaylistIdFromName(AUTODJ_TABLE);
    // If the AutoDJ playlist does not exist yet then create it.
    if (playlistId < 0) {
        playlistId = playlistDAO.createPlaylist(
                AUTODJ_TABLE, PlaylistDAO::PLHT_AUTO_DJ);
        VERIFY_OR_DEBUG_ASSERT(playlistId >= 0) {
            qWarning() << "Failed to create Auto DJ playlist!";
        }
    }
    return playlistId;
}

bool trackFileExists(const TrackPointer& pTrack) {
    if (!pTrack) {
        return false;
    }
    if (!pTrack->getFileInfo().checkFileExists()) {
        qWarning() << "Track does not exist:"
                   << pTrack->getInfo()
                   << pTrack->getLocation();
        return false;
    }
    return true;
}
} // anonymous namespace

AutoDJFeature::AutoDJFeature(Library* pLibrary,
        UserSettingsPointer pConfig,
        PlayerManagerInterface* pPlayerManager)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("autodj")),
          m_pTrackCollection(pLibrary->trackCollectionManager()->internalCollection()),
          m_playlistDao(m_pTrackCollection->getPlaylistDAO()),
          m_iAutoDJPlaylistId(findOrCrateAutoDjPlaylistId(m_playlistDao)),
          m_pAutoDJProcessor(nullptr),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pAutoDJView(nullptr),
          m_viewName(Library::kAutoDJViewName),
          m_autoDjCratesDao(m_iAutoDJPlaylistId, pLibrary->trackCollectionManager(), m_pConfig) {
    qRegisterMetaType<AutoDJProcessor::AutoDJState>("AutoDJState");
    m_pAutoDJProcessor = new AutoDJProcessor(this,
            m_pConfig,
            pPlayerManager,
            pLibrary->trackCollectionManager(),
            m_iAutoDJPlaylistId);

    // Connect loadTrackToPlayer signal as a queued connection to make sure all callbacks of a
    // previous load attempt have been called #10504.
    connect(m_pAutoDJProcessor,
            &AutoDJProcessor::loadTrackToPlayer,
            this,
            &LibraryFeature::loadTrackToPlayer,
            Qt::QueuedConnection);

    m_playlistDao.setAutoDJProcessor(m_pAutoDJProcessor);

    // Create the "Crates" tree-item under the root item.
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    m_pCratesTreeItem = pRootItem->appendChild(tr("Crates"));
    m_pCratesTreeItem->setIcon(QIcon(":/images/library/ic_library_crates.svg"));

    // Create tree-items under "Crates".
    constructCrateChildModel();

    m_pSidebarModel->setRootItem(std::move(pRootItem));

    // Be notified when the status of crates changes.
    connect(m_pTrackCollection,
            &TrackCollection::crateInserted,
            this,
            &AutoDJFeature::slotCrateChanged);
    connect(m_pTrackCollection,
            &TrackCollection::crateUpdated,
            this,
            &AutoDJFeature::slotCrateChanged);
    connect(m_pTrackCollection,
            &TrackCollection::crateDeleted,
            this,
            &AutoDJFeature::slotCrateChanged);

    // Create context-menu items for enabling/disabling the auto-DJ
    m_pEnableAutoDJAction = make_parented<QAction>(tr("Enable Auto DJ"), this);
    connect(m_pEnableAutoDJAction.get(),
            &QAction::triggered,
            this,
            &AutoDJFeature::slotEnableAutoDJ);

    m_pDisableAutoDJAction = make_parented<QAction>(tr("Disable Auto DJ"), this);
    connect(m_pDisableAutoDJAction.get(),
            &QAction::triggered,
            this,
            &AutoDJFeature::slotDisableAutoDJ);

    // Create context-menu item for clearing the auto-DJ queue
    m_pClearQueueAction = make_parented<QAction>(tr("Clear Auto DJ Queue"), this);
    const auto removeKeySequence =
            // TODO(XXX): Qt6 replace enum | with QKeyCombination
            QKeySequence(static_cast<int>(kHideRemoveShortcutModifier) |
                    kHideRemoveShortcutKey);
    m_pClearQueueAction->setShortcut(removeKeySequence);
    connect(m_pClearQueueAction.get(),
            &QAction::triggered,
            this,
            &AutoDJFeature::slotClearQueue);
    // Create context menu item to allow crates to be removed from AutoDJ sources.
    // onRightClickChild() gets the clicked crate's id form the sidebar model and
    // assigns it to this action's data.
    // In slotRemoveCrateFromAutoDj() we retrieve the CrateId data and finally
    // remove the crate from sources in removeCrateFromAutoDj().
    m_pRemoveCrateFromAutoDjAction =
            make_parented<QAction>(tr("Remove Crate as Track Source"), this);
    m_pRemoveCrateFromAutoDjAction->setShortcut(removeKeySequence);
    connect(m_pRemoveCrateFromAutoDjAction.get(),
            &QAction::triggered,
            this,
            &AutoDJFeature::slotRemoveCrateFromAutoDj);
}

AutoDJFeature::~AutoDJFeature() {
    delete m_pAutoDJProcessor;
}

QVariant AutoDJFeature::title() {
    return tr("Auto DJ");
}

void AutoDJFeature::bindLibraryWidget(
        WLibrary* libraryWidget,
        KeyboardEventFilter* keyboard) {
    m_pAutoDJView = new DlgAutoDJ(
            libraryWidget,
            m_pConfig,
            m_pLibrary,
            m_pAutoDJProcessor,
            keyboard);
    libraryWidget->registerView(m_viewName, m_pAutoDJView);
    connect(m_pAutoDJView,
            &DlgAutoDJ::loadTrack,
            this,
            &AutoDJFeature::loadTrack);
    connect(m_pAutoDJView,
            &DlgAutoDJ::loadTrackToPlayer,
            this,
            &LibraryFeature::loadTrackToPlayer);

    connect(m_pAutoDJView,
            &DlgAutoDJ::trackSelected,
            this,
            &AutoDJFeature::trackSelected);

    // Be informed when the user wants to add another random track.
    connect(m_pAutoDJProcessor,
            &AutoDJProcessor::randomTrackRequested,
            this,
            &AutoDJFeature::slotRandomQueue);
    connect(m_pAutoDJView,
            &DlgAutoDJ::addRandomTrackButton,
            this,
            &AutoDJFeature::slotAddRandomTrack);

    // Update shortcuts displayed in the context menu
    QKeySequence toggleAutoDJShortcut = QKeySequence(
            keyboard->getKeyboardConfig()->getValueString(ConfigKey("[AutoDJ]", "enabled")),
            QKeySequence::PortableText);
    m_pEnableAutoDJAction->setShortcut(toggleAutoDJShortcut);
    m_pDisableAutoDJAction->setShortcut(toggleAutoDJShortcut);
}

void AutoDJFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    // store the sidebar widget pointer for later use in onRightClickChild
    m_pSidebarWidget = pSidebarWidget;
}

TreeItemModel* AutoDJFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void AutoDJFeature::activate() {
    //qDebug() << "AutoDJFeature::activate()";
    emit switchToView(m_viewName);
    emit disableSearch();
    emit enableCoverArtDisplay(true);
}

void AutoDJFeature::clear() {
    QMessageBox::StandardButton btn = QMessageBox::question(nullptr,
            tr("Confirmation Clear"),
            tr("Do you really want to remove all tracks from the Auto DJ queue?") +
                    tr("This can not be undone."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (btn == QMessageBox::Yes) {
            m_playlistDao.clearAutoDJQueue();
    }
}

void AutoDJFeature::paste() {
    emit pasteFromSidebar();
}

// Called by SidebarModel
void AutoDJFeature::deleteItem(const QModelIndex& index) {
    TreeItem* pSelectedItem = static_cast<TreeItem*>(index.internalPointer());
    if (!pSelectedItem || pSelectedItem == m_pCratesTreeItem) {
            return;
    }
    CrateId crateId(pSelectedItem->getData());
    removeCrateFromAutoDj(crateId);
}

// Called by deleteItem and slotRemoveCrateFromAutoDj()
void AutoDJFeature::removeCrateFromAutoDj(CrateId crateId) {
    DEBUG_ASSERT(crateId.isValid());
    // TODO Confirm dialog?
    m_pTrackCollection->updateAutoDjCrate(crateId, false);
}

bool AutoDJFeature::dropAccept(const QList<QUrl>& urls, QObject* pSource) {
    // If a track is dropped onto the Auto DJ tree node, but the track isn't in the
    // library, then add the track to the library before adding it to the
    // Auto DJ playlist.
    // pSource != nullptr it is a drop from inside Mixxx and indicates all
    // tracks already in the DB
    const QList<mixxx::FileInfo> fileInfos =
            // collect all tracks, accept playlist files
            DragAndDropHelper::supportedTracksFromUrls(urls, false, true);
    const QList<TrackId> trackIds =
            m_pLibrary->trackCollectionManager()->resolveTrackIds(fileInfos, pSource);
    if (trackIds.isEmpty()) {
        return false;
    }

    // Return whether appendTracksToPlaylist succeeded.
    return m_playlistDao.appendTracksToPlaylist(trackIds, m_iAutoDJPlaylistId);
}

bool AutoDJFeature::dragMoveAccept(const QList<QUrl>& urls) {
    return DragAndDropHelper::urlsContainSupportedTrackFiles(urls, true);
}

void AutoDJFeature::slotEnableAutoDJ() {
    m_pAutoDJProcessor->toggleAutoDJ(true);
}

void AutoDJFeature::slotDisableAutoDJ() {
    m_pAutoDJProcessor->toggleAutoDJ(false);
}

void AutoDJFeature::slotClearQueue() {
    clear();
}

// Add a crate to the AutoDJ sources
void AutoDJFeature::slotAddCrateToAutoDj(CrateId crateId) {
    m_pTrackCollection->updateAutoDjCrate(crateId, true);
}

void AutoDJFeature::slotRemoveCrateFromAutoDj() {
    CrateId crateId(m_pRemoveCrateFromAutoDjAction->data());
    removeCrateFromAutoDj(crateId);
}

void AutoDJFeature::slotCrateChanged(CrateId crateId) {
    Crate crate;
    if (m_pTrackCollection->crates().readCrateById(crateId, &crate) && crate.isAutoDjSource()) {
        // Crate exists and is already a source for AutoDJ
        // -> Find and update the corresponding child item
        for (int i = 0; i < m_crateList.length(); ++i) {
            if (m_crateList[i].getId() == crateId) {
                QModelIndex parentIndex = m_pSidebarModel->index(0, 0);
                QModelIndex childIndex = m_pSidebarModel->index(i, 0, parentIndex);
                m_pSidebarModel->setData(childIndex, crate.getName(), Qt::DisplayRole);
                m_crateList[i] = crate;
                return; // early exit
            }
        }
        // No child item for crate found
        // -> Create and append a new child item for this crate
        // TODO() Use here std::span to get around the heap alloctaion of
        // std::vector for a single element.
        std::vector<std::unique_ptr<TreeItem>> rows;
        rows.push_back(std::make_unique<TreeItem>(crate.getName(), crate.getId().toVariant()));
        QModelIndex parentIndex = m_pSidebarModel->index(0, 0);
        m_pSidebarModel->insertTreeItemRows(std::move(rows), m_crateList.length(), parentIndex);
        m_crateList.append(crate);
    } else {
        // Crate does not exist or is not a source for AutoDJ
        // -> Find and remove the corresponding child item
        for (int i = 0; i < m_crateList.length(); ++i) {
            if (m_crateList[i].getId() == crateId) {
                QModelIndex parentIndex = m_pSidebarModel->index(0, 0);
                m_pSidebarModel->removeRows(i, 1, parentIndex);
                m_crateList.removeAt(i);
                return; // early exit
            }
        }
    }
}

void AutoDJFeature::slotAddRandomTrack() {
    if (m_iAutoDJPlaylistId < 0) {
        qWarning() << "Could not load random track.";
        return;
    }

    TrackPointer pTrack;
    const bool smartQueueEnabled = m_pConfig->getValue(
            ConfigKey(kAutoDjConfigGroup, kEnableSmartQueue), false);
    const bool requireCompatibleKey = m_pConfig->getValue(
            ConfigKey(kAutoDjConfigGroup, kSmartQueueRequireCompatibleKey),
            false);
    if (smartQueueEnabled) {
        pTrack = retrieveSmartTrack();
        if (!pTrack && requireCompatibleKey) {
            // Do not undermine the strict-key preference with pure random.
            qWarning() << "Smart Queue: no key-compatible track available.";
            return;
        }
    }
    if (!pTrack) {
        pTrack = retrieveRandomTrack();
    }

    if (pTrack) {
        m_pTrackCollection->getPlaylistDAO().appendTrackToPlaylist(
                pTrack->getId(), m_iAutoDJPlaylistId);
        if (m_pAutoDJView) {
            m_pAutoDJView->onShow();
        }
        return;
    }
    qWarning() << "Could not load random track.";
}

TrackPointer AutoDJFeature::retrieveRandomTrack() {
    for (int failedRetrieveAttempts = 0;
            failedRetrieveAttempts < 2 * kMaxRetrieveAttempts;
            ++failedRetrieveAttempts) {
        TrackId randomTrackId;
        if (m_crateList.isEmpty()) {
            // Fetch Track from Library since we have no assigned crates
            randomTrackId = m_autoDjCratesDao.getRandomTrackIdFromLibrary(
                    m_iAutoDJPlaylistId);
        } else {
            // Fetch track from crates.
            // We do not fall back to Library if this fails because this
            // may add banned tracks
            randomTrackId = m_autoDjCratesDao.getRandomTrackId();
        }

        if (!randomTrackId.isValid()) {
            continue;
        }

        TrackPointer pTrack =
                m_pLibrary->trackCollectionManager()->getTrackById(randomTrackId);
        VERIFY_OR_DEBUG_ASSERT(pTrack) {
            qWarning() << "Track does not exist:" << randomTrackId;
            continue;
        }
        if (trackFileExists(pTrack)) {
            return pTrack;
        }
    }
    return TrackPointer();
}

TrackPointer AutoDJFeature::retrieveSmartTrack() {
    const bool fromLibrary = m_crateList.isEmpty();

    const double maxBpmDelta = m_pConfig->getValue(
            ConfigKey(kAutoDjConfigGroup, kSmartQueueMaxBpmDelta), 6.0);
    const bool requireCompatibleKey = m_pConfig->getValue(
            ConfigKey(kAutoDjConfigGroup, kSmartQueueRequireCompatibleKey),
            false);

    // The currently playing track is the reference: its BPM/key drive both the
    // database pre-filter and the final ranking.
    double referenceBpm = 0.0;
    mixxx::track::io::key::ChromaticKey referenceKey =
            mixxx::track::io::key::INVALID;
    TrackId currentTrackId;
    const TrackPointer pCurrent = PlayerInfo::instance().getCurrentPlayingTrack();
    if (pCurrent) {
        currentTrackId = pCurrent->getId();
        referenceBpm = pCurrent->getBpm();
        referenceKey = pCurrent->getKey();
    }

    // Build BPM ranges (including half-/double-tempo) for the DB pre-filter.
    // |ref - cand| <= d, |ref - 2*cand| <= d and |ref - cand/2| <= d map to the
    // three candidate-BPM windows below.
    QList<QPair<double, double>> bpmRanges;
    if (mixxx::Bpm::isValidValue(referenceBpm)) {
        const double lo = referenceBpm - maxBpmDelta;
        const double hi = referenceBpm + maxBpmDelta;
        bpmRanges.append(qMakePair(lo, hi));
        bpmRanges.append(qMakePair(lo / 2.0, hi / 2.0));
        bpmRanges.append(qMakePair(lo * 2.0, hi * 2.0));
    }

    // In strict mode, only fetch key-compatible candidates from the database.
    QList<int> keyIds;
    if (requireCompatibleKey && referenceKey != mixxx::track::io::key::INVALID) {
        const QList<mixxx::track::io::key::ChromaticKey> compatible =
                KeyUtils::getCompatibleKeys(referenceKey);
        keyIds.reserve(compatible.size());
        for (const auto key : compatible) {
            keyIds.append(static_cast<int>(key));
        }
    }

    // Base exclusions that always apply: the current track and everything
    // loaded into a deck (we never want to queue a track that's already
    // playing or cued up).
    QList<TrackId> baseExcludeList;
    QSet<TrackId> baseExcludeSet;
    const auto addBaseExclusion = [&](TrackId id) {
        if (id.isValid() && !baseExcludeSet.contains(id)) {
            baseExcludeSet.insert(id);
            baseExcludeList.append(id);
        }
    };
    addBaseExclusion(currentTrackId);
    const int numDecks = PlayerInfo::instance().numDecks();
    for (int i = 0; i < numDecks; ++i) {
        const TrackPointer pDeckTrack = PlayerInfo::instance().getTrackInfo(
                PlayerManager::groupForDeck(i));
        if (pDeckTrack) {
            addBaseExclusion(pDeckTrack->getId());
        }
    }

    // Recency exclusions layer on top of the base set to keep the queue moving
    // forward instead of looping the same tracks.
    QList<TrackId> recentExcludeList = baseExcludeList;
    QSet<TrackId> recentExcludeSet = baseExcludeSet;
    for (const TrackId& id : m_recentlyPickedTrackIds) {
        if (id.isValid() && !recentExcludeSet.contains(id)) {
            recentExcludeSet.insert(id);
            recentExcludeList.append(id);
        }
    }

    const mixxx::aidj::RulesTrackRanker ranker;

    // Run a single selection attempt with the given hard BPM pre-filter and
    // exclusion set, returning the top-ranked track or null if nothing matched.
    // The DB pre-filter is only an optimization: the ranker still applies the
    // soft BPM/key preferences (and the same exclusions) to whatever it gets.
    const auto runAttempt =
            [&](const QList<QPair<double, double>>& ranges,
                    const QList<TrackId>& excludeList,
                    const QSet<TrackId>& excludeSet) -> TrackPointer {
        const QList<TrackId> candidateIds =
                m_autoDjCratesDao.getSmartCandidateTrackIds(
                        fromLibrary,
                        m_iAutoDJPlaylistId,
                        ranges,
                        keyIds,
                        excludeList,
                        kSmartQueueCandidateLimit);

        QList<mixxx::aidj::RankerCandidate> candidates;
        candidates.reserve(candidateIds.size());
        for (const TrackId& trackId : candidateIds) {
            const TrackPointer pTrack =
                    m_pLibrary->trackCollectionManager()->getTrackById(trackId);
            if (!pTrack || !trackFileExists(pTrack)) {
                continue;
            }
            mixxx::aidj::RankerCandidate candidate;
            candidate.trackId = trackId;
            candidate.bpm = pTrack->getBpm();
            candidate.key = pTrack->getKey();
            candidates.append(candidate);
        }
        if (candidates.isEmpty()) {
            return TrackPointer();
        }

        mixxx::aidj::MixingContext context;
        context.maxBpmDelta = maxBpmDelta;
        context.requireCompatibleKey = requireCompatibleKey;
        context.currentTrackId = currentTrackId;
        context.currentBpm = referenceBpm;
        context.currentKey = referenceKey;
        context.excludeTrackIds = excludeSet;

        const QList<TrackId> ranked = ranker.rank(context, candidates);
        if (ranked.isEmpty()) {
            return TrackPointer();
        }
        return m_pLibrary->trackCollectionManager()->getTrackById(ranked.first());
    };

    // Tiered fallback so a narrow pre-filter never stalls the queue:
    //   1) BPM window + full recency (the ideal pick),
    //   2) BPM window but ignore recency (small libraries),
    //   3) drop the hard BPM filter entirely and let the ranker demote
    //      out-of-window / unknown-BPM tracks as designed.
    // The compatible-key filter (strict mode) is kept in every tier, so the
    // "require compatible key" contract is never violated.
    TrackPointer pPicked = runAttempt(bpmRanges, recentExcludeList, recentExcludeSet);
    if (!pPicked) {
        pPicked = runAttempt(bpmRanges, baseExcludeList, baseExcludeSet);
    }
    if (!pPicked && !bpmRanges.isEmpty()) {
        pPicked = runAttempt(
                QList<QPair<double, double>>(), baseExcludeList, baseExcludeSet);
    }

    if (pPicked) {
        rememberPickedTrack(pPicked->getId());
    }
    return pPicked;
}

void AutoDJFeature::rememberPickedTrack(TrackId trackId) {
    if (!trackId.isValid()) {
        return;
    }
    const int historySize = qMax(0,
            m_pConfig->getValue(
                    ConfigKey(kAutoDjConfigGroup, kSmartQueueRecentHistory),
                    kSmartQueueRecentHistoryDefault));
    // Move the track to the most-recent end.
    m_recentlyPickedTrackIds.removeAll(trackId);
    m_recentlyPickedTrackIds.append(trackId);
    while (m_recentlyPickedTrackIds.size() > historySize) {
        m_recentlyPickedTrackIds.removeFirst();
    }
}

void AutoDJFeature::constructCrateChildModel() {
    m_crateList.clear();
    CrateSelectResult autoDjCrates(m_pTrackCollection->crates().selectAutoDjCrates(true));
    Crate crate;
    while (autoDjCrates.populateNext(&crate)) {
        // Create the TreeItem for this crate.
        m_pCratesTreeItem->appendChild(crate.getName(), crate.getId().toVariant());
        m_crateList.append(crate);
    }
}

void AutoDJFeature::onRightClick(const QPoint& globalPos) {
    QMenu menu(m_pSidebarWidget);
    if (m_pAutoDJProcessor->getState() == AutoDJProcessor::ADJ_DISABLED) {
        menu.addAction(m_pEnableAutoDJAction.get());
    } else {
        menu.addAction(m_pDisableAutoDJAction.get());
    }
    menu.addAction(m_pClearQueueAction.get());
    menu.exec(globalPos);
}

void AutoDJFeature::onRightClickChild(const QPoint& globalPos,
        const QModelIndex& index) {
    TreeItem* pClickedItem = static_cast<TreeItem*>(index.internalPointer());
    QMenu menu(m_pSidebarWidget);
    if (m_pCratesTreeItem == pClickedItem) {
        // The "Crates" parent item was right-clicked.
        // Bring up the context menu.
        QMenu crateMenu(m_pSidebarWidget);
        crateMenu.setTitle(tr("Add Crate as Track Source"));
        CrateSelectResult nonAutoDjCrates(m_pTrackCollection->crates().selectAutoDjCrates(false));
        Crate crate;
        while (nonAutoDjCrates.populateNext(&crate)) {
            auto pAction = std::make_unique<QAction>(crate.getName(), &crateMenu);
            auto crateId = crate.getId();
            connect(pAction.get(), &QAction::triggered, this, [this, crateId] {
                slotAddCrateToAutoDj(crateId);
            });
            crateMenu.addAction(pAction.get());
            pAction.release();
        }
        menu.addMenu(&crateMenu);
        menu.exec(globalPos);
    } else {
        // A crate child item was right-clicked.
        // Bring up the context menu.
        m_pRemoveCrateFromAutoDjAction->setData(pClickedItem->getData()); // the selected CrateId
        menu.addAction(m_pRemoveCrateFromAutoDjAction);
        menu.exec(globalPos);
    }
}

void AutoDJFeature::slotRandomQueue(int numTracksToAdd) {
    for (int addCount = 0; addCount < numTracksToAdd; ++addCount) {
        slotAddRandomTrack();
    }
}
