// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_menu_delegate.h"

#include "base/prefs/pref_service.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_node_data.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/event_utils.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/user_metrics.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

using content::PageNavigator;
using content::UserMetricsAction;
using views::MenuItemView;

// Max width of a menu. There does not appear to be an OS value for this, yet
// both IE and FF restrict the max width of a menu.
static const int kMaxMenuWidth = 400;

BookmarkMenuDelegate::BookmarkMenuDelegate(Browser* browser,
                                           PageNavigator* navigator,
                                           views::Widget* parent,
                                           int first_menu_id)
    : browser_(browser),
      profile_(browser->profile()),
      page_navigator_(navigator),
      parent_(parent),
      menu_(NULL),
      for_drop_(false),
      parent_menu_item_(NULL),
      next_menu_id_(first_menu_id),
      real_delegate_(NULL),
      is_mutating_model_(false),
      location_(bookmark_utils::LAUNCH_NONE){
}

BookmarkMenuDelegate::~BookmarkMenuDelegate() {
  BookmarkModelFactory::GetForProfile(profile_)->RemoveObserver(this);
}

void BookmarkMenuDelegate::Init(
    views::MenuDelegate* real_delegate,
    MenuItemView* parent,
    const BookmarkNode* node,
    int start_child_index,
    ShowOptions show_options,
    bookmark_utils::BookmarkLaunchLocation location) {
  BookmarkModelFactory::GetForProfile(profile_)->AddObserver(this);
  real_delegate_ = real_delegate;
  if (parent) {
    parent_menu_item_ = parent;
    int initial_count = parent->GetSubmenu() ?
        parent->GetSubmenu()->GetMenuItemCount() : 0;
    if ((start_child_index < node->child_count()) &&
        (initial_count > 0)) {
      parent->AppendSeparator();
    }
    BuildMenu(node, start_child_index, parent, &next_menu_id_);
    if (show_options == SHOW_PERMANENT_FOLDERS)
      BuildMenusForPermanentNodes(parent, &next_menu_id_);
  } else {
    menu_ = CreateMenu(node, start_child_index, show_options);
  }

  location_ = location;
}

void BookmarkMenuDelegate::SetPageNavigator(PageNavigator* navigator) {
  page_navigator_ = navigator;
  if (context_menu_.get())
    context_menu_->SetPageNavigator(navigator);
}

void BookmarkMenuDelegate::SetActiveMenu(const BookmarkNode* node,
                                         int start_index) {
  DCHECK(!parent_menu_item_);
  if (!node_to_menu_map_[node])
    CreateMenu(node, start_index, HIDE_PERMANENT_FOLDERS);
  menu_ = node_to_menu_map_[node];
}

string16 BookmarkMenuDelegate::GetTooltipText(
    int id,
    const gfx::Point& screen_loc) const {
  DCHECK(menu_id_to_node_map_.find(id) != menu_id_to_node_map_.end());

  MenuIDToNodeMap::const_iterator i = menu_id_to_node_map_.find(id);
  DCHECK(i != menu_id_to_node_map_.end());
  const BookmarkNode* node = i->second;
  if (node->is_url()) {
    return BookmarkBarView::CreateToolTipForURLAndTitle(
        screen_loc, node->url(), node->GetTitle(), profile_,
        parent()->GetNativeView());
  }
  return string16();
}

bool BookmarkMenuDelegate::IsTriggerableEvent(views::MenuItemView* menu,
                                              const ui::Event& e) {
  return e.type() == ui::ET_GESTURE_TAP ||
         e.type() == ui::ET_GESTURE_TAP_DOWN ||
         event_utils::IsPossibleDispositionEvent(e);
}

void BookmarkMenuDelegate::ExecuteCommand(int id, int mouse_event_flags) {
  DCHECK(menu_id_to_node_map_.find(id) != menu_id_to_node_map_.end());

  const BookmarkNode* node = menu_id_to_node_map_[id];
  std::vector<const BookmarkNode*> selection;
  selection.push_back(node);

  chrome::OpenAll(parent_->GetNativeWindow(), page_navigator_, selection,
                  ui::DispositionFromEventFlags(mouse_event_flags),
                  profile_);
  bookmark_utils::RecordBookmarkLaunch(location_);
}

bool BookmarkMenuDelegate::ShouldExecuteCommandWithoutClosingMenu(
    int id, const ui::Event& event) {
  return (event.flags() & ui::EF_LEFT_MOUSE_BUTTON) &&
         ui::DispositionFromEventFlags(event.flags()) == NEW_BACKGROUND_TAB;
}

bool BookmarkMenuDelegate::GetDropFormats(
    MenuItemView* menu,
    int* formats,
    std::set<ui::OSExchangeData::CustomFormat>* custom_formats) {
  *formats = ui::OSExchangeData::URL;
  custom_formats->insert(BookmarkNodeData::GetBookmarkCustomFormat());
  return true;
}

bool BookmarkMenuDelegate::AreDropTypesRequired(MenuItemView* menu) {
  return true;
}

bool BookmarkMenuDelegate::CanDrop(MenuItemView* menu,
                                   const ui::OSExchangeData& data) {
  // Only accept drops of 1 node, which is the case for all data dragged from
  // bookmark bar and menus.

  if (!drop_data_.Read(data) || drop_data_.elements.size() != 1 ||
      !profile_->GetPrefs()->GetBoolean(prefs::kEditBookmarksEnabled))
    return false;

  if (drop_data_.has_single_url())
    return true;

  const BookmarkNode* drag_node = drop_data_.GetFirstNode(profile_);
  if (!drag_node) {
    // Dragging a folder from another profile, always accept.
    return true;
  }

  // Drag originated from same profile and is not a URL. Only accept it if
  // the dragged node is not a parent of the node menu represents.
  if (menu_id_to_node_map_.find(menu->GetCommand()) ==
      menu_id_to_node_map_.end()) {
    // If we don't know the menu assume its because we're embedded. We'll
    // figure out the real operation when GetDropOperation is invoked.
    return true;
  }
  const BookmarkNode* drop_node = menu_id_to_node_map_[menu->GetCommand()];
  DCHECK(drop_node);
  while (drop_node && drop_node != drag_node)
    drop_node = drop_node->parent();
  return (drop_node == NULL);
}

int BookmarkMenuDelegate::GetDropOperation(
    MenuItemView* item,
    const ui::DropTargetEvent& event,
    views::MenuDelegate::DropPosition* position) {
  // Should only get here if we have drop data.
  DCHECK(drop_data_.is_valid());

  const BookmarkNode* node = menu_id_to_node_map_[item->GetCommand()];
  const BookmarkNode* drop_parent = node->parent();
  int index_to_drop_at = drop_parent->GetIndexOf(node);
  switch (*position) {
    case views::MenuDelegate::DROP_AFTER:
      if (node == BookmarkModelFactory::GetForProfile(
              profile_)->other_node() ||
          node == BookmarkModelFactory::GetForProfile(
              profile_)->mobile_node()) {
        // Dropping after these nodes makes no sense.
        *position = views::MenuDelegate::DROP_NONE;
      }
      index_to_drop_at++;
      break;

    case views::MenuDelegate::DROP_BEFORE:
      if (node == BookmarkModelFactory::GetForProfile(
              profile_)->mobile_node()) {
        // Dropping before this node makes no sense.
        *position = views::MenuDelegate::DROP_NONE;
      }
      break;

    case views::MenuDelegate::DROP_ON:
      drop_parent = node;
      index_to_drop_at = node->child_count();
      break;

    default:
      break;
  }
  DCHECK(drop_parent);
  return bookmark_utils::BookmarkDropOperation(
      profile_, event, drop_data_, drop_parent, index_to_drop_at);
}

int BookmarkMenuDelegate::OnPerformDrop(
    MenuItemView* menu,
    views::MenuDelegate::DropPosition position,
    const ui::DropTargetEvent& event) {
  const BookmarkNode* drop_node = menu_id_to_node_map_[menu->GetCommand()];
  DCHECK(drop_node);
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile_);
  DCHECK(model);
  const BookmarkNode* drop_parent = drop_node->parent();
  DCHECK(drop_parent);
  int index_to_drop_at = drop_parent->GetIndexOf(drop_node);
  switch (position) {
    case views::MenuDelegate::DROP_AFTER:
      index_to_drop_at++;
      break;

    case views::MenuDelegate::DROP_ON:
      DCHECK(drop_node->is_folder());
      drop_parent = drop_node;
      index_to_drop_at = drop_node->child_count();
      break;

    case views::MenuDelegate::DROP_BEFORE:
      if (drop_node == model->other_node() ||
          drop_node == model->mobile_node()) {
        // This can happen with SHOW_PERMANENT_FOLDERS.
        drop_parent = model->bookmark_bar_node();
        index_to_drop_at = drop_parent->child_count();
      }
      break;

    default:
      break;
  }

  int result = bookmark_utils::PerformBookmarkDrop(
      profile_, drop_data_, drop_parent, index_to_drop_at);
  return result;
}

bool BookmarkMenuDelegate::ShowContextMenu(MenuItemView* source,
                                           int id,
                                           const gfx::Point& p,
                                           bool is_mouse_gesture) {
  DCHECK(menu_id_to_node_map_.find(id) != menu_id_to_node_map_.end());
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(menu_id_to_node_map_[id]);
  bool close_on_delete = !parent_menu_item_ &&
      (nodes[0]->parent() == BookmarkModelFactory::GetForProfile(
          profile())->other_node() &&
       nodes[0]->parent()->child_count() == 1);
  context_menu_.reset(
      new BookmarkContextMenu(
          parent_,
          browser_,
          profile_,
          page_navigator_,
          nodes[0]->parent(),
          nodes,
          close_on_delete));
  context_menu_->set_observer(this);
  context_menu_->RunMenuAt(p);
  context_menu_.reset(NULL);
  return true;
}

bool BookmarkMenuDelegate::CanDrag(MenuItemView* menu) {
  const BookmarkNode* node = menu_id_to_node_map_[menu->GetCommand()];
  // Don't let users drag the other folder.
  return node->parent() != BookmarkModelFactory::GetForProfile(
      profile_)->root_node();
}

void BookmarkMenuDelegate::WriteDragData(MenuItemView* sender,
                                         ui::OSExchangeData* data) {
  DCHECK(sender && data);

  content::RecordAction(UserMetricsAction("BookmarkBar_DragFromFolder"));

  BookmarkNodeData drag_data(menu_id_to_node_map_[sender->GetCommand()]);
  drag_data.Write(profile_, data);
}

int BookmarkMenuDelegate::GetDragOperations(MenuItemView* sender) {
  return bookmark_utils::BookmarkDragOperation(
      profile_, menu_id_to_node_map_[sender->GetCommand()]);
}

int BookmarkMenuDelegate::GetMaxWidthForMenu(MenuItemView* menu) {
  return kMaxMenuWidth;
}

void BookmarkMenuDelegate::BookmarkModelChanged() {
}

void BookmarkMenuDelegate::BookmarkNodeFaviconChanged(
    BookmarkModel* model, const BookmarkNode* node) {
  NodeToMenuIDMap::iterator menu_pair = node_to_menu_id_map_.find(node);
  if (menu_pair == node_to_menu_id_map_.end())
    return;  // We're not showing a menu item for the node.

  // Iterate through the menus looking for the menu containing node.
  for (NodeToMenuMap::iterator i(node_to_menu_map_.begin());
       i != node_to_menu_map_.end(); ++i) {
    MenuItemView* menu_item = i->second->GetMenuItemByID(menu_pair->second);
    if (menu_item) {
      const gfx::Image& favicon = model->GetFavicon(node);
      menu_item->SetIcon(favicon.AsImageSkia());
      return;
    }
  }

  if (parent_menu_item_) {
    MenuItemView* menu_item = parent_menu_item_->GetMenuItemByID(
        menu_pair->second);
    if (menu_item) {
      const gfx::Image& favicon = model->GetFavicon(node);
      menu_item->SetIcon(favicon.AsImageSkia());
    }
  }
}

void BookmarkMenuDelegate::WillRemoveBookmarks(
    const std::vector<const BookmarkNode*>& bookmarks) {
  DCHECK(!is_mutating_model_);
  is_mutating_model_ = true;

  // Remove the observer so that when the remove happens we don't prematurely
  // cancel the menu. The observer ias added back in DidRemoveBookmarks.
  BookmarkModelFactory::GetForProfile(profile_)->RemoveObserver(this);

  // Remove the menu items.
  std::set<MenuItemView*> changed_parent_menus;
  for (std::vector<const BookmarkNode*>::const_iterator i(bookmarks.begin());
       i != bookmarks.end(); ++i) {
    NodeToMenuIDMap::iterator node_to_menu = node_to_menu_id_map_.find(*i);
    if (node_to_menu != node_to_menu_id_map_.end()) {
      MenuItemView* menu = GetMenuByID(node_to_menu->second);
      DCHECK(menu);  // If there an entry in node_to_menu_id_map_, there should
                     // be a menu.
      MenuItemView* parent = menu->GetParentMenuItem();
      DCHECK(parent);
      changed_parent_menus.insert(parent);
      parent->RemoveMenuItemAt(menu->parent()->GetIndexOf(menu));
      node_to_menu_id_map_.erase(node_to_menu);
    }
  }

  // All the bookmarks in |bookmarks| should have the same parent. It's possible
  // to support different parents, but this would need to prune any nodes whose
  // parent has been removed. As all nodes currently have the same parent, there
  // is the DCHECK.
  DCHECK(changed_parent_menus.size() <= 1);

  for (std::set<MenuItemView*>::const_iterator i(changed_parent_menus.begin());
       i != changed_parent_menus.end(); ++i)
    (*i)->ChildrenChanged();

  // Remove any descendants of the removed nodes in |node_to_menu_id_map_|.
  for (NodeToMenuIDMap::iterator i(node_to_menu_id_map_.begin());
       i != node_to_menu_id_map_.end(); ) {
    bool ancestor_removed = false;
    for (std::vector<const BookmarkNode*>::const_iterator j(bookmarks.begin());
         j != bookmarks.end(); ++j) {
      if (i->first->HasAncestor(*j)) {
        ancestor_removed = true;
        break;
      }
    }
    if (ancestor_removed)
      node_to_menu_id_map_.erase(i++);
    else
      ++i;
  }
}

void BookmarkMenuDelegate::DidRemoveBookmarks() {
  // Balances remove in WillRemoveBookmarksImpl.
  BookmarkModelFactory::GetForProfile(profile_)->AddObserver(this);
  DCHECK(is_mutating_model_);
  is_mutating_model_ = false;
}

MenuItemView* BookmarkMenuDelegate::CreateMenu(const BookmarkNode* parent,
                                               int start_child_index,
                                               ShowOptions show_options) {
  MenuItemView* menu = new MenuItemView(real_delegate_);
  menu->SetCommand(next_menu_id_++);
  menu_id_to_node_map_[menu->GetCommand()] = parent;
  menu->set_has_icons(true);
  BuildMenu(parent, start_child_index, menu, &next_menu_id_);
  if (show_options == SHOW_PERMANENT_FOLDERS)
    BuildMenusForPermanentNodes(menu, &next_menu_id_);
  node_to_menu_map_[parent] = menu;
  return menu;
}

void BookmarkMenuDelegate::BuildMenusForPermanentNodes(
    views::MenuItemView* menu,
    int* next_menu_id) {
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile_);
  bool added_separator = false;
  BuildMenuForPermanentNode(model->other_node(), menu, next_menu_id,
                            &added_separator);
  BuildMenuForPermanentNode(model->mobile_node(), menu, next_menu_id,
                            &added_separator);
}

void BookmarkMenuDelegate::BuildMenuForPermanentNode(
    const BookmarkNode* node,
    MenuItemView* menu,
    int* next_menu_id,
    bool* added_separator) {
  if (!node->IsVisible() || node->GetTotalNodeCount() == 1)
    return;  // No children, don't create a menu.

  if (!*added_separator) {
    *added_separator = true;
    menu->AppendSeparator();
  }
  int id = *next_menu_id;
  (*next_menu_id)++;
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  gfx::ImageSkia* folder_icon = rb.GetImageSkiaNamed(IDR_BOOKMARK_BAR_FOLDER);
  MenuItemView* submenu = menu->AppendSubMenuWithIcon(
      id, node->GetTitle(), *folder_icon);
  BuildMenu(node, 0, submenu, next_menu_id);
  menu_id_to_node_map_[id] = node;
}

void BookmarkMenuDelegate::BuildMenu(const BookmarkNode* parent,
                                     int start_child_index,
                                     MenuItemView* menu,
                                     int* next_menu_id) {
  DCHECK(parent->empty() || start_child_index < parent->child_count());
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  for (int i = start_child_index; i < parent->child_count(); ++i) {
    const BookmarkNode* node = parent->GetChild(i);
    int id = *next_menu_id;

    (*next_menu_id)++;
    if (node->is_url()) {
      const gfx::Image& image = BookmarkModelFactory::GetForProfile(
          profile_)->GetFavicon(node);
      const gfx::ImageSkia* icon = image.IsEmpty() ?
          rb.GetImageSkiaNamed(IDR_DEFAULT_FAVICON) : image.ToImageSkia();
      menu->AppendMenuItemWithIcon(id, node->GetTitle(), *icon);
      node_to_menu_id_map_[node] = id;
    } else if (node->is_folder()) {
      gfx::ImageSkia* folder_icon =
          rb.GetImageSkiaNamed(IDR_BOOKMARK_BAR_FOLDER);
      MenuItemView* submenu = menu->AppendSubMenuWithIcon(
          id, node->GetTitle(), *folder_icon);
      node_to_menu_id_map_[node] = id;
      BuildMenu(node, 0, submenu, next_menu_id);
    } else {
      NOTREACHED();
    }
    menu_id_to_node_map_[id] = node;
  }
}

MenuItemView* BookmarkMenuDelegate::GetMenuByID(int id) {
  for (NodeToMenuMap::const_iterator i(node_to_menu_map_.begin());
       i != node_to_menu_map_.end(); ++i) {
    MenuItemView* menu = i->second->GetMenuItemByID(id);
    if (menu)
      return menu;
  }

  return parent_menu_item_ ? parent_menu_item_->GetMenuItemByID(id) : NULL;
}
