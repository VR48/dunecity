/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <GUI/dune/CityBudgetWindow.h>

#include <globals.h>
#include <Game.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <Command.h>

#include <FileClasses/TextManager.h>
#include <FileClasses/GFXManager.h>
#include <misc/format.h>

CityBudgetWindow::CityBudgetWindow()
 : Window(100, 100, 400, 460) {

    // Non-modal: clicks outside this window dismiss it and pass through to
    // the underlying interface, so the player can still hit build buttons,
    // scroll the starport list, and select units without first finding the
    // Close button.
    setModal(false);

    setWindowWidget(&mainVBox);

    mainVBox.addWidget(VSpacer::create(10));

    titleLabel.setText("City Budget");
    titleLabel.setAlignment(Alignment_HCenter);
    titleLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&titleLabel);
    mainVBox.addWidget(VSpacer::create(15));

    // Top: year + current player credits at-a-glance.
    yearLabel.setText("Year: 0");
    yearLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&yearLabel);
    mainVBox.addWidget(VSpacer::create(4));

    treasuryLabel.setText("Credits: 0");
    treasuryLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&treasuryLabel);
    mainVBox.addWidget(VSpacer::create(12));

    // Tax rate slider — player-adjustable lever (0-20%, default 7%).
    taxLabel.setText("Tax Rate:");
    taxLabel.setTextColor(COLOR_WHITE);
    taxHBox.addWidget(&taxLabel);
    taxHBox.addWidget(HSpacer::create(10));

    taxMinus.setTextures(pGFXManager->getUIGraphic(UI_Minus), pGFXManager->getUIGraphic(UI_Minus_Pressed));
    taxMinus.setOnClick(std::bind(&CityBudgetWindow::onTaxDecrease, this));
    taxHBox.addWidget(&taxMinus);
    taxHBox.addWidget(HSpacer::create(5));

    taxValueLabel.setText("7%");
    taxValueLabel.setTextColor(COLOR_WHITE);
    taxHBox.addWidget(&taxValueLabel);
    taxHBox.addWidget(HSpacer::create(5));

    taxPlus.setTextures(pGFXManager->getUIGraphic(UI_Plus), pGFXManager->getUIGraphic(UI_Plus_Pressed));
    taxPlus.setOnClick(std::bind(&CityBudgetWindow::onTaxIncrease, this));
    taxHBox.addWidget(&taxPlus);

    mainVBox.addWidget(&taxHBox, 30);
    mainVBox.addWidget(VSpacer::create(8));

    incomeLabel.setText("Tax Revenue: +0/yr");
    incomeLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&incomeLabel);
    mainVBox.addWidget(VSpacer::create(4));

    policeCostLabel.setText("Police: -0/yr");
    policeCostLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&policeCostLabel);
    mainVBox.addWidget(VSpacer::create(4));

    netLabel.setText("Net: 0/yr");
    netLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&netLabel);
    mainVBox.addWidget(VSpacer::create(4));

    perSecondLabel.setText("Income: 0 credits/sec");
    perSecondLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&perSecondLabel);
    mainVBox.addWidget(VSpacer::create(15));

    // Police funding slider — the only player-adjustable lever.
    policeLabel.setText("Police Funding:");
    policeLabel.setTextColor(COLOR_WHITE);
    policeHBox.addWidget(&policeLabel);
    policeHBox.addWidget(HSpacer::create(10));

    policeMinus.setTextures(pGFXManager->getUIGraphic(UI_Minus), pGFXManager->getUIGraphic(UI_Minus_Pressed));
    policeMinus.setOnClick(std::bind(&CityBudgetWindow::onPoliceDecrease, this));
    policeHBox.addWidget(&policeMinus);
    policeHBox.addWidget(HSpacer::create(5));

    policeValueLabel.setText("100%");
    policeValueLabel.setTextColor(COLOR_WHITE);
    policeHBox.addWidget(&policeValueLabel);
    policeHBox.addWidget(HSpacer::create(5));

    policePlus.setTextures(pGFXManager->getUIGraphic(UI_Plus), pGFXManager->getUIGraphic(UI_Plus_Pressed));
    policePlus.setOnClick(std::bind(&CityBudgetWindow::onPoliceIncrease, this));
    policeHBox.addWidget(&policePlus);

    mainVBox.addWidget(&policeHBox, 30);
    mainVBox.addWidget(VSpacer::create(15));

    // Population breakdown (informational).
    resPopLabel.setText("Residential: 0");
    resPopLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&resPopLabel);
    mainVBox.addWidget(VSpacer::create(4));

    comPopLabel.setText("Commercial: 0");
    comPopLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&comPopLabel);
    mainVBox.addWidget(VSpacer::create(4));

    indPopLabel.setText("Industrial: 0");
    indPopLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&indPopLabel);
    mainVBox.addWidget(VSpacer::create(4));

    totalPopLabel.setText("Total Population: 0");
    totalPopLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&totalPopLabel);
    mainVBox.addWidget(VSpacer::create(4));

    unemploymentLabel.setText("Unemployment: 0%");
    unemploymentLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&unemploymentLabel);
    mainVBox.addWidget(VSpacer::create(4));

    hospitalLabel.setText("Hospitals: 0");
    hospitalLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&hospitalLabel);
    mainVBox.addWidget(VSpacer::create(4));

    churchLabel.setText("Churches: 0");
    churchLabel.setTextColor(COLOR_WHITE);
    mainVBox.addWidget(&churchLabel);
    mainVBox.addWidget(VSpacer::create(15));

    applyButton.setText("Apply");
    applyButton.setOnClick(std::bind(&CityBudgetWindow::onApply, this));
    buttonsHBox.addWidget(&applyButton);
    buttonsHBox.addWidget(HSpacer::create(10));

    closeButton.setText("Close");
    closeButton.setOnClick(std::bind(&CityBudgetWindow::onClose, this));
    buttonsHBox.addWidget(&closeButton);

    mainVBox.addWidget(&buttonsHBox, 30);
    mainVBox.addWidget(VSpacer::create(10));

    // Snapshot the live tax rate and funding % so the sliders open at the
    // current settings rather than the header defaults. updateDisplay()
    // refreshes the readouts but does NOT clobber pendingTaxRate /
    // pendingPolicePercent on later ticks (the player may already be
    // mid-edit).
    auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (citySim && citySim->isInitialized()) {
        pendingPolicePercent = citySim->getPoliceFundingPercent();
        pendingTaxRate       = citySim->getCityTax();
    }
    updateDisplay();
}

CityBudgetWindow::~CityBudgetWindow() = default;

void CityBudgetWindow::draw(Point position) {
    updateDisplay();
    Window::draw(position);
}

void CityBudgetWindow::onClose() {
    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void CityBudgetWindow::onPoliceIncrease() {
    if (pendingPolicePercent < 100) {
        pendingPolicePercent += 5;
        if (pendingPolicePercent > 100) pendingPolicePercent = 100;
        updateAllocationLabels();
    }
}

void CityBudgetWindow::onPoliceDecrease() {
    if (pendingPolicePercent > 0) {
        pendingPolicePercent -= 5;
        if (pendingPolicePercent < 0) pendingPolicePercent = 0;
        updateAllocationLabels();
    }
}

void CityBudgetWindow::onTaxIncrease() {
    if (pendingTaxRate < DuneCity::CitySimulation::kMaxTaxRate) {
        ++pendingTaxRate;
        updateAllocationLabels();
    }
}

void CityBudgetWindow::onTaxDecrease() {
    if (pendingTaxRate > DuneCity::CitySimulation::kMinTaxRate) {
        --pendingTaxRate;
        updateAllocationLabels();
    }
}

void CityBudgetWindow::onApply() {
    // Route through the command system so multiplayer remains
    // deterministic. p0 reserved (legacy houseID slot for tax),
    // p1 = new tax rate.
    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_CITY_SET_TAX_RATE,
                0u, static_cast<uint32_t>(pendingTaxRate), 0u));
    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_CITY_SET_BUDGET,
                static_cast<uint32_t>(pendingPolicePercent), 0u, 0u));
    updateDisplay();
}

void CityBudgetWindow::updateAllocationLabels() {
    taxValueLabel.setText(fmt::sprintf("%d%%", pendingTaxRate));
    policeValueLabel.setText(fmt::sprintf("%d%%", pendingPolicePercent));
}

void CityBudgetWindow::updateDisplay() {
    auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (!citySim || !citySim->isInitialized()) {
        return;
    }

    yearLabel.setText(fmt::sprintf("Year: %d", citySim->getCityYear()));
    treasuryLabel.setText(fmt::sprintf("Credits: %d", citySim->getTotalFunds()));

    // Projected annual revenue using the pending tax slider and land value.
    const int totalPop  = citySim->getTotalPop();
    const int taxRate   = pendingTaxRate;
    const int avgLV     = citySim->getAvgLandValue();
    const int projected = DuneCity::computeAnnualTaxRevenue(totalPop, taxRate, avgLV);
    incomeLabel.setText(fmt::sprintf("Tax Revenue: +%d/yr (proj.)", projected));

    // Police: nominal cost is full-funded; actual paid is scaled by the
    // selected funding percentage, including pending slider changes.
    const int32_t nominal = citySim->getNominalPoliceCost();
    const int32_t paying  = (nominal * pendingPolicePercent) / 100;
    policeCostLabel.setText(fmt::sprintf("Police: -%d/yr (of %d at 100%%)",
                                         paying, nominal));

    const int32_t netAnnual = projected - paying;
    netLabel.setText(fmt::sprintf("Net: %+d/yr", netAnnual));
    // At default game speed (16ms/cycle), 1 city year ≈ 60 seconds.
    const int32_t perSec = netAnnual / 60;
    perSecondLabel.setText(fmt::sprintf("Income: %+d credits/sec", perSec));

    // The slider's pending value is seeded once in the constructor so
    // subsequent +/- clicks edit the pending copy without being clobbered.
    updateAllocationLabels();

    resPopLabel.setText(fmt::sprintf("Residential: %d", citySim->getDisplayResPop()));
    comPopLabel.setText(fmt::sprintf("Commercial: %d", citySim->getDisplayComPop()));
    indPopLabel.setText(fmt::sprintf("Industrial: %d", citySim->getDisplayIndPop()));
    totalPopLabel.setText(fmt::sprintf("Total Population: %d", citySim->getDisplayTotalPop()));

    // Unemployment
    const int unemp = citySim->getUnemploymentRate();
    unemploymentLabel.setText(fmt::sprintf("Unemployment: %d%%", unemp));
    unemploymentLabel.setTextColor(unemp > 20 ? COLOR_RGB(255,80,80) : COLOR_WHITE);

    // Hospital/church count (auto-created by game on residential zones)
    hospitalLabel.setText(fmt::sprintf("Hospitals: %d", citySim->getHospitalCount()));
    churchLabel.setText(fmt::sprintf("Churches: %d", citySim->getChurchCount()));
}
