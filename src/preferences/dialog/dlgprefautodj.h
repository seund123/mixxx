#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefautodjdlg.h"
#include "preferences/usersettings.h"

class QWidget;

class DlgPrefAutoDJ : public DlgPreferencePage, public Ui::DlgPrefAutoDJDlg {
    Q_OBJECT
  public:
    DlgPrefAutoDJ(QWidget* pParent, UserSettingsPointer pConfig);

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private slots:
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    void slotToggleRequeueIgnore(Qt::CheckState state);
#else
    void slotToggleRequeueIgnore(int buttonState);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    void slotToggleRandomQueue(Qt::CheckState state);
#else
    void slotToggleRandomQueue(int buttonState);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    void slotToggleSmartQueue(Qt::CheckState state);
#else
    void slotToggleSmartQueue(int buttonState);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    void slotToggleAutoSyncOnTransition(Qt::CheckState state);
#else
    void slotToggleAutoSyncOnTransition(int buttonState);
#endif

  private:
    void considerRepeatPlaylistState(bool);

    UserSettingsPointer m_pConfig;
};
