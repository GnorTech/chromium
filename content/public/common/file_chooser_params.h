// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_COMMON_FILE_CHOOSER_PARAMS_H_
#define CONTENT_PUBLIC_COMMON_FILE_CHOOSER_PARAMS_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/string16.h"
#include "content/common/content_export.h"

namespace content {

// Struct used by WebContentsDelegate.
struct CONTENT_EXPORT FileChooserParams {
  FileChooserParams();
  ~FileChooserParams();

  enum Mode {
    // Requires that the file exists before allowing the user to pick it.
    Open,

    // Like Open, but allows picking multiple files to open.
    OpenMultiple,

    // Like Open, but selects a folder.
    OpenFolder,

    // Allows picking a nonexistent file, and prompts to overwrite if the file
    // already exists.
    Save,
  };

  Mode mode;

  bool extract_directory;

  // Title to be used for the dialog. This may be empty for the default title,
  // which will be either "Open" or "Save" depending on the mode.
  string16 title;

  // Default file name to select in the dialog.
  base::FilePath default_file_name;

  // A list of valid lower-cased MIME types or file extensions specified in an
  // input element. It is used to restrict selectable files to such types.
  std::vector<string16> accept_types;

#if defined(OS_ANDROID)
  // Used to select items other than files, i.e. camera/mic. See
  // SelectFileDialog.java for more details.
  // TODO(jrg): upstream SelectFileDialog.java!  Currently lives in chrome/.
  string16 capture;
#endif
};

}  // namespace content

#endif  // CONTENT_PUBLIC_COMMON_FILE_CHOOSER_PARAMS_H_
