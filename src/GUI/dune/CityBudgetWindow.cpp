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

#include <algorithm>

namespace {

constexpr int kBudgetWindowWidth = 420;
constexpr int kBudgetWindowHeight = 380;

Uint32 centeredCoordinate(int available, int extent) {
    return static_cast<Uint32>(std::max(0, (available - extent) / 2));
}

void configureSectionHeading(Label& label, const char* text) {
    label.setText(text);
    label.setTextFontSize(12);
    label.setTextColor(COLOR_RGB(128, 24, 0));
}

void configureValueLabel(Label& label, Alignment_Enum alignment = Alignment_Left) {
    label.setTextColor(COLOR_WHITE);
    label.setTextFontSize(13);
    label.setAlignment(alignment);
}

} // namespace

CityBudgetWindow::CityBudgetWindow()
 : Window(centeredCoordinate(settings.video.width - SIDEBARWIDTH, kBudgetWindowWidth),
          centeredCoordinate(settings.video.height, kBudgetWindowHeight),
          kBudgetWindowWidth, kBudgetWindowHeight) {

    // Non-modal: clicks outside this window dismiss it and pass through to
    // the underlying interface, so the player can still hit build buttons,
    // scroll the starport list, and select units without first finding the
    // Close button.
    setModal(false);

    setWindowWidget(&rootHBox);
    rootHBox.addWidget(HSpacer::create(10));
    rootHBox.addWidget(&mainVBox);
    rootHBox.addWidget(HSpacer::create(10));

    mainVBox.addWidget(VSpacer::create(8));

    titleLabel.setText("City Budget");
    titleLabel.setAlignment(Alignment_HCenter);
    titleLabel.setTextColor(COLOR_WHITE);
    titleLabel.setTextFontSize(16);
    mainVBox.addWidget(&titleLabel, 22);
    mainVBox.addWidget(VSpacer::create(4));

    yearLabel.setText("Year: 0");
    treasuryLabel.setText("Treasury: 0 credits");
    configureValueLabel(yearLabel);
    configureValueLabel(treasuryLabel, Alignment_Right);
    summaryHBox.addWidget(&yearLabel);
    summaryHBox.addWidget(HSpacer::create(8));
    summaryHBox.addWidget(&treasuryLabel);
    mainVBox.addWidget(&summaryHBox, 24);
    mainVBox.addWidget(VSpacer::create(8));

    configureSectionHeading(allocationsHeadingLabel, "ALLOCATIONS");
    mainVBox.addWidget(&allocationsHeadingLabel, 18);

    // Tax rate slider — player-adjustable lever (0-20%, default 7%).
    taxLabel.setText("Tax Rate");
    configureValueLabel(taxLabel);
    taxHBox.addWidget(&taxLabel);
    taxHBox.addWidget(HSpacer::create(8));

    taxMinus.setTextures(pGFXManager->getUIGraphic(UI_Minus), pGFXManager->getUIGraphic(UI_Minus_Pressed));
    taxMinus.setOnClick(std::bind(&CityBudgetWindow::onTaxDecrease, this));
    taxHBox.addWidget(&taxMinus);
    taxHBox.addWidget(HSpacer::create(5));

    taxValueLabel.setText("7%");
    configureValueLabel(taxValueLabel, Alignment_HCenter);
    taxHBox.addWidget(&taxValueLabel, 54);
    taxHBox.addWidget(HSpacer::create(5));

    taxPlus.setTextures(pGFXManager->getUIGraphic(UI_Plus), pGFXManager->getUIGraphic(UI_Plus_Pressed));
    taxPlus.setOnClick(std::bind(&CityBudgetWindow::onTaxIncrease, this));
    taxHBox.addWidget(&taxPlus);

    mainVBox.addWidget(&taxHBox, 32);

    policeLabel.setText("Police Funding");
    configureValueLabel(policeLabel);
    policeHBox.addWidget(&policeLabel);
    policeHBox.addWidget(HSpacer::create(8));

    policeMinus.setTextures(pGFXManager->getUIGraphic(UI_Minus), pGFXManager->getUIGraphic(UI_Minus_Pressed));
    policeMinus.setOnClick(std::bind(&CityBudgetWindow::onPoliceDecrease, this));
    policeHBox.addWidget(&policeMinus);
    policeHBox.addWidget(HSpacer::create(5));

    policeValueLabel.setText("100%");
    configureValueLabel(policeValueLabel, Alignment_HCenter);
    policeHBox.addWidget(&policeValueLabel, 54);
    policeHBox.addWidget(HSpacer::create(5));

    policePlus.setTextures(pGFXManager->getUIGraphic(UI_Plus), pGFXManager->getUIGraphic(UI_Plus_Pressed));
    policePlus.setOnClick(std::bind(&CityBudgetWindow::onPoliceIncrease, this));
    policeHBox.addWidget(&policePlus);

    mainVBox.addWidget(&policeHBox, 32);
    mainVBox.addWidget(VSpacer::create(6));

    configureSectionHeading(forecastHeadingLabel, "ANNUAL FORECAST");
    mainVBox.addWidget(&forecastHeadingLabel, 18);

    incomeLabel.setText("Projected Tax: +0/yr");
    policeCostLabel.setText("Police Services: -0/yr");
    netLabel.setText("Net Annual: 0/yr");
    perSecondLabel.setText("Cash Flow: 0/sec");
    configureValueLabel(incomeLabel);
    configureValueLabel(policeCostLabel, Alignment_Right);
    configureValueLabel(netLabel);
    configureValueLabel(perSecondLabel, Alignment_Right);
    forecastPrimaryHBox.addWidget(&incomeLabel);
    forecastPrimaryHBox.addWidget(HSpacer::create(8));
    forecastPrimaryHBox.addWidget(&policeCostLabel);
    forecastSecondaryHBox.addWidget(&netLabel);
    forecastSecondaryHBox.addWidget(HSpacer::create(8));
    forecastSecondaryHBox.addWidget(&perSecondLabel);
    mainVBox.addWidget(&forecastPrimaryHBox, 22);
    mainVBox.addWidget(&forecastSecondaryHBox, 22);
    mainVBox.addWidget(VSpacer::create(6));

    configureSectionHeading(cityStatusHeadingLabel, "CITY STATUS");
    mainVBox.addWidget(&cityStatusHeadingLabel, 18);

    totalPopLabel.setText("Population: 0");
    unemploymentLabel.setText("Unemployment: 0%");
    configureValueLabel(totalPopLabel);
    configureValueLabel(unemploymentLabel, Alignment_Right);
    populationHBox.addWidget(&totalPopLabel);
    populationHBox.addWidget(HSpacer::create(8));
    populationHBox.addWidget(&unemploymentLabel);
    mainVBox.addWidget(&populationHBox, 22);

    resPopLabel.setText("Residential: 0");
    comPopLabel.setText("Commercial: 0");
    indPopLabel.setText("Industrial: 0");
    configureValueLabel(resPopLabel);
    configureValueLabel(comPopLabel, Alignment_HCenter);
    configureValueLabel(indPopLabel, Alignment_Right);
    zoningHBox.addWidget(&resPopLabel);
    zoningHBox.addWidget(HSpacer::create(8));
    zoningHBox.addWidget(&comPopLabel);
    zoningHBox.addWidget(HSpacer::create(8));
    zoningHBox.addWidget(&indPopLabel);
    mainVBox.addWidget(&zoningHBox, 22);

    servicesLabel.setText("Services: 0 hospitals | 0 churches");
    configureValueLabel(servicesLabel);
    mainVBox.addWidget(&servicesLabel, 22);
    mainVBox.addWidget(VSpacer::create(10));

    confirmButton.setText("Confirm");
    confirmButton.setOnClick(std::bind(&CityBudgetWindow::onConfirm, this));
    buttonsHBox.addWidget(&confirmButton);
    buttonsHBox.addWidget(HSpacer::create(10));

    cancelButton.setText("Cancel");
    cancelButton.setOnClick(std::bind(&CityBudgetWindow::onCancel, this));
    buttonsHBox.addWidget(&cancelButton);

    mainVBox.addWidget(&buttonsHBox, 34);
    mainVBox.addWidget(VSpacer::create(8));

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

void CityBudgetWindow::onCancel() {
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

void CityBudgetWindow::onConfirm() {
    // Route through the command system so multiplayer remains
    // deterministic. p0 reserved (legacy houseID slot for tax),
    // p1 = new tax rate.
    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_CITY_SET_TAX_RATE,
                0u, static_cast<uint32_t>(pendingTaxRate), 0u));
    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_CITY_SET_BUDGET,
                static_cast<uint32_t>(pendingPolicePercent), 0u, 0u));
    onCancel();
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
    treasuryLabel.setText(fmt::sprintf("Treasury: %d credits", citySim->getTotalFunds()));

    // Projected annual revenue using the pending tax slider and land value.
    const int totalPop  = citySim->getTotalPop();
    const int taxRate   = pendingTaxRate;
    const int avgLV     = citySim->getAvgLandValue();
    const int projected = DuneCity::computeAnnualTaxRevenue(totalPop, taxRate, avgLV);
    incomeLabel.setText(fmt::sprintf("Projected Tax: +%d/yr", projected));

    // Police: nominal cost is full-funded; actual paid is scaled by the
    // selected funding percentage, including pending slider changes.
    const int32_t nominal = citySim->getNominalPoliceCost();
    const int32_t paying  = (nominal * pendingPolicePercent) / 100;
    policeCostLabel.setText(fmt::sprintf("Police Services: -%d/yr", paying));

    const int32_t netAnnual = projected - paying;
    netLabel.setText(fmt::sprintf("Net Annual: %+d/yr", netAnnual));
    // At default game speed (16ms/cycle), 1 city year ≈ 60 seconds.
    const int32_t perSec = netAnnual / 60;
    perSecondLabel.setText(fmt::sprintf("Cash Flow: %+d/sec", perSec));

    // The slider's pending value is seeded once in the constructor so
    // subsequent +/- clicks edit the pending copy without being clobbered.
    updateAllocationLabels();

    resPopLabel.setText(fmt::sprintf("Residential: %d", citySim->getDisplayResPop()));
    comPopLabel.setText(fmt::sprintf("Commercial: %d", citySim->getDisplayComPop()));
    indPopLabel.setText(fmt::sprintf("Industrial: %d", citySim->getDisplayIndPop()));
    totalPopLabel.setText(fmt::sprintf("Population: %d", citySim->getDisplayTotalPop()));

    // Unemployment
    const int unemp = citySim->getUnemploymentRate();
    unemploymentLabel.setText(fmt::sprintf("Unemployment: %d%%", unemp));
    unemploymentLabel.setTextColor(unemp > 20 ? COLOR_RGB(255,80,80) : COLOR_WHITE);

    // Hospital/church count (auto-created by game on residential zones)
    servicesLabel.setText(fmt::sprintf("Services: %d hospitals | %d churches",
                                       citySim->getHospitalCount(), citySim->getChurchCount()));
}
