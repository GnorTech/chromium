// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/auto_login_parser/auto_login_parser.h"

#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/string_split.h"
#include "net/base/escape.h"
#include "net/url_request/url_request.h"

namespace components {
namespace auto_login {

namespace {

const char kHeaderName[] = "X-Auto-Login";

bool MatchRealm(const std::string& realm, RealmRestriction restriction) {
  switch (restriction) {
    case ONLY_GOOGLE_COM:
      return realm == "com.google";
    case ALLOW_ANY_REALM:
      return true;
    default:
      NOTREACHED();
      return false;
  }
}

}  // namespace

HeaderData::HeaderData() {}
HeaderData::~HeaderData() {}

bool ParseHeader(const std::string& header,
                 RealmRestriction realm_restriction,
                 HeaderData* header_data) {
  // TODO(pliard): Investigate/fix potential internationalization issue. It
  // seems that "account" from the x-auto-login header might contain non-ASCII
  // characters.
  if (header.empty())
    return false;

  std::vector<std::pair<std::string, std::string> > pairs;
  if (!base::SplitStringIntoKeyValuePairs(header, '=', '&', &pairs))
    return false;

  // Parse the information from the |header| string.
  HeaderData local_params;
  for (size_t i = 0; i < pairs.size(); ++i) {
    const std::pair<std::string, std::string>& pair = pairs[i];
    std::string unescaped_value(net::UnescapeURLComponent(
          pair.second, net::UnescapeRule::URL_SPECIAL_CHARS));
    if (pair.first == "realm") {
      if (!MatchRealm(unescaped_value, realm_restriction))
        return false;
      local_params.realm = unescaped_value;
    } else if (pair.first == "account") {
      local_params.account = unescaped_value;
    } else if (pair.first == "args") {
      local_params.args = unescaped_value;
    }
  }
  if (local_params.realm.empty() || local_params.args.empty())
    return false;

  *header_data = local_params;
  return true;
}

bool ParserHeaderInResponse(net::URLRequest* request,
                            RealmRestriction realm_restriction,
                            HeaderData* header_data) {
  std::string header_string;
  request->GetResponseHeaderByName(kHeaderName, &header_string);
  return ParseHeader(header_string, realm_restriction, header_data);
}

}  // namespace auto_login
}  // namespace components
