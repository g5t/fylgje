// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Mouse interaction for the Detailed tab plots
///
/// Ctrl+Click on a 3×3 grid cell   → zoom into that cell (single-plot view)
/// Alt+Click  on a 3×3 grid cell   → switch between the 9-triplets and
///                                   9-types views for the selected cell
/// Double-click on any plot         → reset zoom to full range
/// Hover                            → tooltip with axis values and count
//===----------------------------------------------------------------------===//
#include "AppWindow.h"
#include "./ui_AppWindow.h"
#include "HistogramManager.h"
#include <QTimer>

void MainWindow::setup_detailed_interactions() {
  // Click navigation (Ctrl / Alt modifiers handled in the callback)
  plots->set_click_callback([this](int i, int j, QMouseEvent * ev){
    on_plot_clicked(i, j, ev);
  });

  // Hover tooltip
  plots->set_hover_callback([this](int i, int j, QMouseEvent * ev){
    on_plot_hovered(i, j, ev);
  });

  // Double-click: reset zoom for that plot, then refresh
  plots->set_double_click_callback([this](int i, int j){
    plots->reset_zoom(i, j);
    plot();
  });
}

// ── Ctrl / Alt + Click navigation ────────────────────────────────────────────

void MainWindow::on_plot_clicked(int i, int j, QMouseEvent * event) {
  if (is_paused()) return;
  auto mods = event->modifiers();
  if (!(mods & Qt::ControlModifier) && !(mods & Qt::AltModifier)) return;

  auto pt = selected_plot_type();

  // Defer so empty_layout() never deletes the plot while its signal is on the stack.
  QTimer::singleShot(0, this, [this, mods, i, j, pt]() {
    if (mods & Qt::ControlModifier) {
      if (pt == PlotType::Triplets) {
        // 9-triplets → single: remember source, fix the clicked triplet
        _previous_plot_type = PlotType::Triplets;
        int triplet = i * 3 + j;
        {QSignalBlocker b(ui->tripletBox); ui->tripletBox->setChecked(true);}
        _fixed_triplet = triplet;
        set_triplet_radio(triplet);
        update_intensity_limits();
        plot();
      } else if (pt == PlotType::Types) {
        // 9-types → single: remember source, fix the clicked type
        _previous_plot_type = PlotType::Types;
        auto t = type_order[i * 3 + j];
        {QSignalBlocker b(ui->intTypeBox); ui->intTypeBox->setChecked(true);}
        _fixed_type = t;
        set_type_radio(t);
        update_intensity_limits();
        plot();
      } else if (pt == PlotType::Singular) {
        // Single → return to whichever 3x3 view we came from
        if (_previous_plot_type == PlotType::Types) {
          // came from 9-types (triplet fixed, type was added): remove the type fix
          {QSignalBlocker b(ui->intTypeBox); ui->intTypeBox->setChecked(false);}
        } else {
          // came from 9-triplets (type fixed, triplet was added): remove the triplet fix
          {QSignalBlocker b(ui->tripletBox); ui->tripletBox->setChecked(false);}
        }
        update_intensity_limits();
        plot();
      }
    } else if (mods & Qt::AltModifier) {
      if (pt == PlotType::Triplets) {
        // 9-triplets → switch to 9-types for the clicked triplet
        _previous_plot_type = PlotType::Types;
        int triplet = i * 3 + j;
        {QSignalBlocker b(ui->tripletBox); ui->tripletBox->setChecked(true);}
        {QSignalBlocker b(ui->intTypeBox); ui->intTypeBox->setChecked(false);}
        _fixed_triplet = triplet;
        set_triplet_radio(triplet);
        update_intensity_limits();
        plot();
      } else if (pt == PlotType::Types) {
        // 9-types → switch to 9-triplets for the clicked type
        _previous_plot_type = PlotType::Triplets;
        auto t = type_order[i * 3 + j];
        {QSignalBlocker b(ui->intTypeBox); ui->intTypeBox->setChecked(true);}
        {QSignalBlocker b(ui->tripletBox); ui->tripletBox->setChecked(false);}
        _fixed_type = t;
        set_type_radio(t);
        update_intensity_limits();
        plot();
      } else if (pt == PlotType::Singular) {
        // Single → switch to the OTHER 3x3 view (opposite of where we came from)
        if (_previous_plot_type == PlotType::Triplets) {
          // came from 9-triplets, switch to 9-types: keep triplet fixed, unfix type
          {QSignalBlocker b(ui->intTypeBox); ui->intTypeBox->setChecked(false);}
          _previous_plot_type = PlotType::Types;
        } else {
          // came from 9-types, switch to 9-triplets: keep type fixed, unfix triplet
          {QSignalBlocker b(ui->tripletBox); ui->tripletBox->setChecked(false);}
          _previous_plot_type = PlotType::Triplets;
        }
        update_intensity_limits();
        plot();
      }
    }
  });
}

// ── Hover tooltip ─────────────────────────────────────────────────────────────

void MainWindow::on_plot_hovered(int i, int j, QMouseEvent * event) {
  auto * p = plots->plot_at(i, j);
  if (!p) return;

  auto d = plots->dim(i, j);
  if (d == PlotManager::Dim::none) return;

  auto t    = plots->type_at(i, j);
  bool flip = plots->is_flipped(i, j);
  auto names = ::bifrost::data::axes_names(t);

  double xc = p->xAxis->pixelToCoord(event->position().x());
  double yc = p->yAxis->pixelToCoord(event->position().y());

  if (d == PlotManager::Dim::one) {
    // For flipped plots, the variable axis is yAxis
    double var_val = flip ? yc : xc;
    double cnt_val = flip ? xc : yc;
    QString var_name = names.empty() ? "bin" : QString::fromStdString(names[0]);
    p->setToolTip(QString("%1: %2  Count: %3")
      .arg(var_name)
      .arg(var_val, 0, 'g', 4)
      .arg(cnt_val, 0, 'f', 0));
  } else if (d == PlotManager::Dim::two) {
    auto * img = plots->image_at(i, j);
    double count = img ? img->data()->data(xc, yc) : 0.0;
    // For flipped 2D, colormap key=yAxis, value=xAxis
    double kv = flip ? yc : xc;
    double vv = flip ? xc : yc;
    QString kn = names.size() > 0 ? QString::fromStdString(names[0]) : "x";
    QString vn = names.size() > 1 ? QString::fromStdString(names[1]) : "y";
    p->setToolTip(QString("%1: %2  %3: %4  Count: %5")
      .arg(kn).arg(kv, 0, 'g', 4)
      .arg(vn).arg(vv, 0, 'g', 4)
      .arg(count, 0, 'f', 0));
  }
}
