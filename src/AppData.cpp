// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
//
/// \file
//
/// \brief GUI interface for saving stored data to HDF5 files
//===----------------------------------------------------------------------===//
#include "AppWindow.h"
#include "./ui_AppWindow.h"

void MainWindow::setup_data(const std::optional<std::string> & output){
  connect(ui->actionSaveHDF5, &QAction::triggered, this, &MainWindow::save_data);

  if (output.has_value()){
    // if output isn't a path, make it one relative to the current working directory
    auto p = std::filesystem::path(output.value());
    if (const auto d = p.parent_path(); d.empty()){
      const auto cwd = std::filesystem::current_path();
      p = cwd / p;
    }
    default_filename = p.string();
  }
}

static bool is_hdf5_extension(const std::string & ext) {
  return ext == ".h5" || ext == ".hdf5";
}

void MainWindow::save_data() {
  using namespace std::filesystem;
  const auto q_default = QString::fromStdString(default_filename);
  const auto q_filename = QFileDialog::getSaveFileName(this, tr("Save Data"), q_default, tr("HDF5 files (*.h5 *.H5 *.hdf5 *.HDF5)"));
  auto p = path(q_filename.toStdString());
  if (!p.has_filename()) return;
  auto ext = std::string(p.extension());
  std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c){return std::tolower(c);});
  if (!is_hdf5_extension(ext)){
    p.replace_extension(path(".h5"));
  }
  try{
    data->save_to(p);
  } catch (const std::runtime_error & ex){
    QMessageBox msgBox;
    const auto txt = fmt::format("Saving data to {} failed", std::string(p));
    msgBox.setText(txt.c_str());
    msgBox.setDetailedText(ex.what());
    msgBox.exec();
  }
}