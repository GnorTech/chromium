// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_POWER_POWER_API_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_POWER_POWER_API_H_

#include "chrome/browser/extensions/extension_function.h"

namespace extensions {

// Implementation of the chrome.experimental.power.requestKeepAwake API.
class PowerRequestKeepAwakeFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("experimental.power.requestKeepAwake",
                             EXPERIMENTAL_POWER_REQUESTKEEPAWAKE)

 protected:
  virtual ~PowerRequestKeepAwakeFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

// Implementation of the chrome.experimental.power.releaseKeepAwake API.
class PowerReleaseKeepAwakeFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("experimental.power.releaseKeepAwake",
                             EXPERIMENTAL_POWER_RELEASEKEEPAWAKE)

 protected:
  virtual ~PowerReleaseKeepAwakeFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_POWER_POWER_API_H_
