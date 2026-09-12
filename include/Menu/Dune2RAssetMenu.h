/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef DUNE2RASSETMENU_H
#define DUNE2RASSETMENU_H

#include "MenuBase.h"

#include <GUI/DropDownBox.h>
#include <GUI/Label.h>
#include <GUI/ProgressBar.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <mod/Dune2RAssetManager.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>

class Dune2RAssetMenu final : public MenuBase {
public:
    Dune2RAssetMenu();
    ~Dune2RAssetMenu() override;

    void update() override;

private:
    void onSelectionChanged(bool interactive);
    void onDownload();
    void onRefreshCatalog();
    void populatePacks();
    void onBack();
    void refreshSelectionStatus();
    std::vector<std::string> selectedPackIDs() const;

    StaticContainer windowWidget;
    Label titleLabel;
    Label introLabel;
    Label packLabel;
    Label statusLabel;
    DropDownBox packDropDown;
    TextProgressBar progressBar;
    TextButton downloadButton;
    TextButton refreshButton;
    TextButton backButton;

    std::unique_ptr<Dune2RAssetManager> assetManager;
    std::future<Dune2RAssetInstallResult> installTask;
    std::atomic<uint64_t> completedBytes{0};
    std::atomic<uint64_t> totalBytes{0};
    std::mutex progressTextMutex;
    std::string progressPack;
    std::string progressFile;
    bool downloading = false;
    bool refreshingCatalog = false;
};

#endif // DUNE2RASSETMENU_H
