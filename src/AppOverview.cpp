// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Detector overview panel for the Fylgje application
//===----------------------------------------------------------------------===//
#include "AppWindow.h"
#include "./ui_AppWindow.h"
#include "PlotManager.h"
#include <array>
#include <cmath>

void MainWindow::setup_overview() {
  // Populate the colormap combo with the same options as the Detailed tab
  ui->overviewColormapCombo->clear();
  for (const auto & name: {"gray", "hot", "cold", "night", "candy", "thermal"}) {
    ui->overviewColormapCombo->addItem(QString(name));
  }
  ui->overviewColormapCombo->setCurrentText(QString(configuration.Plot.ColorGradient.c_str()));
  ui->overviewInvertedCheck->setChecked(configuration.Plot.InvertGradient);
  ui->overviewLogButton->setChecked(configuration.Plot.LogScale);

  // Derive display dimensions from the pixel manager
  const auto & pm = data->pixels();
  int n_cols = pm.num_triplets() * pm.num_pixels();  // 9 * 100 = 900
  int n_rows = pm.num_arcs() * pm.num_tubes();       // 5 * 3  = 15

  // Create the overview QCustomPlot
  auto plot = new QCustomPlot();
  ui->overviewPlotGrid->addWidget(plot, 0, 0);
  plot->xAxis->setRange(0, n_cols);
  plot->yAxis->setRange(0, n_rows);
  plot->axisRect()->setupFullAxesBox();
  plot->xAxis->setLabel("Pixel column (triplet × 100 + position)");
  plot->yAxis->setLabel("Layer (arc × 3 + tube)");
  plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

  // Create colour map with correct dimensions and range
  auto image = new QCPColorMap(plot->xAxis, plot->yAxis);
  image->data()->setSize(n_cols, n_rows);
  image->data()->setRange(QCPRange(0, n_cols), QCPRange(0, n_rows));
  image->setInterpolate(false);
  image->setTightBoundary(false);

  // Attach a colour scale legend on the right
  auto scale = new QCPColorScale(plot);
  scale->setType(QCPAxis::atRight);
  image->setColorScale(scale);
  image->setGradient(named_colormap(configuration.Plot.ColorGradient,
                                    configuration.Plot.InvertGradient));
  image->setDataRange(QCPRange(0, 1));

  overview_plot = plot;
  overview_image = image;

  // Connect controls
  connect(ui->overviewAutoscaleButton, &QPushButton::toggled, this, [this](bool checked) {
    ui->overviewMaxSpin->setReadOnly(checked);
    if (!checked) get_overview_limits();
    else auto_overview_limits();
    plot_overview();
  });
  connect(ui->overviewColormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::plot_overview);
  connect(ui->overviewInvertedCheck, &QCheckBox::toggled, this, &MainWindow::plot_overview);
  connect(ui->overviewLogButton, &QPushButton::toggled, this, &MainWindow::plot_overview);
  connect(ui->overviewMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &MainWindow::get_overview_limits);

  // Hover tooltip: show pixel identifiers and count under the cursor
  connect(plot, &QCustomPlot::mouseMove, this,
          [this, n_cols, n_rows](QMouseEvent * event) {
    // Map screen pixel → axis coordinate → cell index
    double xc = overview_plot->xAxis->pixelToCoord(event->position().x());
    double yc = overview_plot->yAxis->pixelToCoord(event->position().y());
    int col = static_cast<int>(std::floor(xc));
    int row = static_cast<int>(std::floor(yc));
    if (col < 0 || col >= n_cols || row < 0 || row >= n_rows) {
      overview_plot->setToolTip({});
      return;
    }
    // Decompose display coords into detector identifiers
    const auto & pm = data->pixels();
    int n_tubes    = pm.num_tubes();
    int n_pix      = pm.num_pixels();
    int n_triplets = pm.num_triplets();
    int arc     = row / n_tubes;
    int tube    = row % n_tubes;
    int triplet = col / n_pix;
    int pix     = col % n_pix;
    int pta     = n_triplets * n_pix;
    int pa      = n_tubes * pta;
    int pixel_id = pa * arc + pta * tube + n_pix * triplet + pix + 1; // 1-based
    int count    = pm.counts()[pixel_id - 1];
    static constexpr std::array<double, 5> arc_mev{2.7, 3.2, 3.8, 4.4, 5.0};
    overview_plot->setToolTip(
      QString("Arc %1 (%2 meV)  Triplet %3  Tube %4  Pos %5\nPixel ID: %6  Count: %7")
        .arg(arc + 1)
        .arg(arc < static_cast<int>(arc_mev.size()) ? arc_mev[arc] : 0.0)
        .arg(triplet + 1)
        .arg(tube + 1)
        .arg(pix)
        .arg(pixel_id)
        .arg(count)
    );
  });

  // Double-click to reset zoom to the full detector view
  connect(plot, &QCustomPlot::mouseDoubleClick, this,
          [this, n_cols, n_rows](QMouseEvent *) {
    overview_plot->xAxis->setRange(0, n_cols);
    overview_plot->yAxis->setRange(0, n_rows);
    overview_plot->replot();
  });
}

void MainWindow::auto_overview_limits() {
  auto m = data->pixels().max_count();
  overview_max = (m > 0) ? m : 1;
  ui->overviewMaxSpin->setValue(overview_max);
}

void MainWindow::get_overview_limits() {
  overview_max = ui->overviewMaxSpin->value();
}

void MainWindow::plot_overview() {
  if (is_paused()) return;
  if (overview_plot == nullptr || overview_image == nullptr) return;

  if (ui->overviewAutoscaleButton->isChecked()) {
    auto_overview_limits();
  } else {
    get_overview_limits();
  }

  const auto & pm = data->pixels();
  const auto & pd = pm.counts();
  int n_arcs     = pm.num_arcs();      // 5
  int n_tubes    = pm.num_tubes();     // 3
  int n_triplets = pm.num_triplets();  // 9
  int n_pix      = pm.num_pixels();    // 100
  int pta = n_triplets * n_pix;        // pixels_per_tube_arc  = 900
  int pa  = n_tubes * pta;             // pixels_per_arc       = 2700

  for (int arc = 0; arc < n_arcs; ++arc) {
    for (int tube = 0; tube < n_tubes; ++tube) {
      int row = arc * n_tubes + tube;
      for (int triplet = 0; triplet < n_triplets; ++triplet) {
        for (int pix = 0; pix < n_pix; ++pix) {
          int col = triplet * n_pix + pix;
          // Mirrors the corrected offset in PixelManager::pixel():
          //   offset = pixels_per_arc * arc + pixels_per_tube_arc * tube
          //            + pixels_per_tube * triplet
          int idx = pa * arc + pta * tube + n_pix * triplet + pix;
          overview_image->data()->setCell(col, row, pd[idx]);
        }
      }
    }
  }

  auto gradient_name = ui->overviewColormapCombo->currentText().toStdString();
  auto is_inverted   = ui->overviewInvertedCheck->isChecked();
  auto is_log        = ui->overviewLogButton->isChecked();

  overview_image->setGradient(named_colormap(gradient_name, is_inverted));
  overview_image->setDataScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
  overview_image->setDataRange(QCPRange(0, static_cast<double>(overview_max)));

  overview_plot->replot();
}
