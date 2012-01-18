// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webui/web_ui.h"

#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "content/browser/child_process_security_policy.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"
#include "content/browser/webui/generic_handler.h"
#include "content/common/view_messages.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/content_switches.h"

using content::WebContents;
using content::WebUIController;
using content::WebUIMessageHandler;

namespace content {

const WebUI::TypeID WebUI::kNoWebUI = NULL;

// static
string16 WebUI::GetJavascriptCall(
    const std::string& function_name,
    const std::vector<const Value*>& arg_list) {
  string16 parameters;
  std::string json;
  for (size_t i = 0; i < arg_list.size(); ++i) {
    if (i > 0)
      parameters += char16(',');

    base::JSONWriter::Write(arg_list[i], false, &json);
    parameters += UTF8ToUTF16(json);
  }
  return ASCIIToUTF16(function_name) +
      char16('(') + parameters + char16(')') + char16(';');
}

}

WebUI::WebUI(WebContents* contents)
    : hide_favicon_(false),
      focus_location_bar_by_default_(false),
      should_hide_url_(false),
      link_transition_type_(content::PAGE_TRANSITION_LINK),
      bindings_(content::BINDINGS_POLICY_WEB_UI),
      web_contents_(contents) {
  DCHECK(contents);
  AddMessageHandler(new GenericHandler());
}

WebUI::~WebUI() {
  // Delete the controller first, since it may also be keeping a pointer to some
  // of the handlers and can call them at destruction.
  controller_.reset();
  STLDeleteContainerPointers(handlers_.begin(), handlers_.end());
}

// WebUI, public: -------------------------------------------------------------

bool WebUI::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(WebUI, message)
    IPC_MESSAGE_HANDLER(ViewHostMsg_WebUISend, OnWebUISend)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void WebUI::OnWebUISend(const GURL& source_url,
                        const std::string& message,
                        const ListValue& args) {
  if (!ChildProcessSecurityPolicy::GetInstance()->
          HasWebUIBindings(web_contents_->GetRenderProcessHost()->GetID())) {
    NOTREACHED() << "Blocked unauthorized use of WebUIBindings.";
    return;
  }

  if (controller_->OverrideHandleWebUIMessage(source_url, message,args))
    return;

  // Look up the callback for this message.
  MessageCallbackMap::const_iterator callback =
      message_callbacks_.find(message);
  if (callback != message_callbacks_.end()) {
    // Forward this message and content on.
    callback->second.Run(&args);
  }
}

void WebUI::RenderViewCreated(RenderViewHost* render_view_host) {
  controller_->RenderViewCreated(render_view_host);

  // Do not attempt to set the toolkit property if WebUI is not enabled, e.g.,
  // the bookmarks manager page.
  if (!(bindings_ & content::BINDINGS_POLICY_WEB_UI))
    return;

#if defined(TOOLKIT_VIEWS)
  render_view_host->SetWebUIProperty("toolkit", "views");
#elif defined(TOOLKIT_GTK)
  render_view_host->SetWebUIProperty("toolkit", "GTK");
#endif  // defined(TOOLKIT_VIEWS)

  // Let the WebUI know that we're looking for UI that's optimized for touch
  // input.
  // TODO(rbyers) Figure out the right model for enabling touch-optimized UI
  // (http://crbug.com/105380).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kTouchOptimizedUI))
    render_view_host->SetWebUIProperty("touchOptimized", "true");
}

WebContents* WebUI::GetWebContents() const {
  return web_contents_;
}

bool WebUI::ShouldHideFavicon() const {
  return hide_favicon_;
}

void WebUI::HideFavicon() {
  hide_favicon_ = true;
}

bool WebUI::ShouldFocusLocationBarByDefault() const {
  return focus_location_bar_by_default_;
}

void WebUI::FocusLocationBarByDefault() {
  focus_location_bar_by_default_ = true;
}

bool WebUI::ShouldHideURL() const {
  return should_hide_url_;
}

void WebUI::HideURL() {
  should_hide_url_ = true;
}

const string16& WebUI::GetOverriddenTitle() const {
  return overridden_title_;
}

void WebUI::OverrideTitle(const string16& title) {
  overridden_title_ = title;
}

content::PageTransition WebUI::GetLinkTransitionType() const {
  return link_transition_type_;
}

void WebUI::SetLinkTransitionType(content::PageTransition type) {
  link_transition_type_ = type;
}

int WebUI::GetBindings() const {
  return bindings_;
}

void WebUI::SetBindings(int bindings) {
  bindings_ = bindings;
}

void WebUI::SetFrameXPath(const std::string& xpath) {
  frame_xpath_ = xpath;
}

WebUIController* WebUI::GetController() const {
  return controller_.get();
}

void WebUI::SetController(WebUIController* controller) {
  controller_.reset(controller);
}

void WebUI::CallJavascriptFunction(const std::string& function_name) {
  DCHECK(IsStringASCII(function_name));
  string16 javascript = ASCIIToUTF16(function_name + "();");
  ExecuteJavascript(javascript);
}

void WebUI::CallJavascriptFunction(const std::string& function_name,
                                   const Value& arg) {
  DCHECK(IsStringASCII(function_name));
  std::vector<const Value*> args;
  args.push_back(&arg);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUI::CallJavascriptFunction(
    const std::string& function_name,
    const Value& arg1, const Value& arg2) {
  DCHECK(IsStringASCII(function_name));
  std::vector<const Value*> args;
  args.push_back(&arg1);
  args.push_back(&arg2);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUI::CallJavascriptFunction(
    const std::string& function_name,
    const Value& arg1, const Value& arg2, const Value& arg3) {
  DCHECK(IsStringASCII(function_name));
  std::vector<const Value*> args;
  args.push_back(&arg1);
  args.push_back(&arg2);
  args.push_back(&arg3);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUI::CallJavascriptFunction(
    const std::string& function_name,
    const Value& arg1,
    const Value& arg2,
    const Value& arg3,
    const Value& arg4) {
  DCHECK(IsStringASCII(function_name));
  std::vector<const Value*> args;
  args.push_back(&arg1);
  args.push_back(&arg2);
  args.push_back(&arg3);
  args.push_back(&arg4);
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUI::CallJavascriptFunction(
    const std::string& function_name,
    const std::vector<const Value*>& args) {
  DCHECK(IsStringASCII(function_name));
  ExecuteJavascript(GetJavascriptCall(function_name, args));
}

void WebUI::RegisterMessageCallback(const std::string &message,
                                    const MessageCallback& callback) {
  message_callbacks_.insert(std::make_pair(message, callback));
}

void WebUI::ProcessWebUIMessage(const GURL& source_url,
                                const std::string& message,
                                const base::ListValue& args) {
  OnWebUISend(source_url, message, args);
}

// WebUI, protected: ----------------------------------------------------------

void WebUI::AddMessageHandler(WebUIMessageHandler* handler) {
  DCHECK(!handler->web_ui());
  handler->set_web_ui(this);
  handler->RegisterMessages();
  handlers_.push_back(handler);
}

void WebUI::ExecuteJavascript(const string16& javascript) {
  web_contents_->GetRenderViewHost()->ExecuteJavascriptInWebFrame(
      ASCIIToUTF16(frame_xpath_), javascript);
}
