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

#ifndef CITYBUDGETWINDOW_H
#define CITYBUDGETWINDOW_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <GUI/Spacer.h>
#include <GUI/PictureButton.h>

/// City budget mini-window. Player can adjust the tax rate and the
/// police-funding share; both feed back into the city budget. Underfunding
/// police scales coverage proportionally.
class CityBudgetWindow : public Window
{
public:
    CityBudgetWindow();
    virtual ~CityBudgetWindow();

    void draw(Point position) override;

    void onClose();
    void onTaxIncrease();
    void onTaxDecrease();
    void onPoliceIncrease();
    void onPoliceDecrease();
    void onApply();

    static CityBudgetWindow* create() {
        CityBudgetWindow* dlg = new CityBudgetWindow();
        dlg->pAllocated = true;
        return dlg;
    }

private:
    void updateDisplay();
    void updateAllocationLabels();

    VBox mainVBox;
    Label titleLabel;

    Label yearLabel;
    Label treasuryLabel;
    Label incomeLabel;
    Label policeCostLabel;
    Label netLabel;
    Label perSecondLabel;

    HBox taxHBox;
    Label taxLabel;
    PictureButton taxMinus;
    Label taxValueLabel;
    PictureButton taxPlus;

    HBox policeHBox;
    Label policeLabel;
    PictureButton policeMinus;
    Label policeValueLabel;
    PictureButton policePlus;

    Label resPopLabel;
    Label comPopLabel;
    Label indPopLabel;
    Label totalPopLabel;

    HBox buttonsHBox;
    TextButton applyButton;
    TextButton closeButton;

    int pendingPolicePercent = 100;
    int pendingTaxRate = 7;
};

#endif // CITYBUDGETWINDOW_H
