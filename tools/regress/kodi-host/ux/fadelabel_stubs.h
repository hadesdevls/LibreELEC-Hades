// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Stand-ins for the parts of Kodi's GUI that GUIFadeLabelControl.cpp uses, so that the real file
// can be compiled and run on the build host (fadelabel_depth_test.cpp). Test-only, not in the tree.
#pragma once

#include "utils/TransformMatrix.h"

#include <cstdint>
#include <string>
#include <vector>

// --- GUIControl.h
class CGUIListItem;
class CGUIMessage;
struct CDirtyRegion
{
};
using CDirtyRegionList = std::vector<CDirtyRegion>;
enum GUICONTROLTYPES
{
  GUICONTROL_UNKNOWN,
  GUICONTROL_FADELABEL
};

class CGUIControl
{
public:
  CGUIControl(int parentID, int controlID, float posX, float posY, float width, float height)
    : m_parentID(parentID), m_controlID(controlID), m_posX(posX), m_posY(posY), m_width(width),
      m_height(height)
  {
  }
  CGUIControl(const CGUIControl&) = default;
  virtual ~CGUIControl() = default;
  virtual CGUIControl* Clone() const = 0;
  virtual void Process(unsigned int, CDirtyRegionList&) {}
  virtual void Render() {}
  virtual bool CanFocus() const { return true; }
  virtual bool OnMessage(CGUIMessage&) { return false; }
  virtual void AssignDepth(); // as Kodi's: the depth of this pass (fadelabel_depth_test.cpp)
  virtual bool UpdateColors(const CGUIListItem*) { return false; }
  virtual std::string GetDescription() const { return {}; }
  void MarkDirtyRegion(unsigned int = 0) {}
  int GetID() const { return m_controlID; }
  int GetParentID() const { return m_parentID; }

protected:
  int m_parentID;
  int m_controlID;
  float m_posX;
  float m_posY;
  float m_width;
  float m_height;
  TransformMatrix m_cachedTransform;
  GUICONTROLTYPES ControlType = GUICONTROL_UNKNOWN;
};

// --- GUIInfoLabel.h, GUIInfoColor
namespace KODI::GUILIB::GUIINFO
{
class CGUIInfoLabel
{
public:
  CGUIInfoLabel(const std::string& label, const std::string& = "", int = 0) : m_label(label) {}
  const std::string& GetLabel(int, bool = false, std::string* = nullptr) const { return m_label; }

private:
  std::string m_label;
};
struct CGUIInfoColor
{
  bool Update() { return false; }
  operator uint32_t() const { return 0xFFFFFFFF; }
};
} // namespace KODI::GUILIB::GUIINFO

// --- GUIFont.h, GUITextLayout.h, GUILabel.h
constexpr uint32_t XBFONT_LEFT = 0;
constexpr uint32_t XBFONT_CENTER_X = 2;
constexpr uint32_t XBFONT_CENTER_Y = 4;

class CGUIFont
{
public:
  float GetCharWidth(wchar_t) const { return 10.0f; }
};

class CScrollInfo
{
public:
  CScrollInfo(unsigned int = 50, float = 0.0f, int speed = 60) { SetSpeed(speed); }
  void SetSpeed(int speed) { m_pixelSpeed = speed * 0.001f; }
  void Reset()
  {
    m_pixelPos = 0;
    m_textWidth = 0;
  }
  float m_pixelPos{0};
  float m_pixelSpeed{0};
  std::u32string m_suffix;
  mutable float m_textWidth{0};
};

class CGUITextLayout
{
public:
  CGUITextLayout(CGUIFont*, bool, float = 0.0f, CGUIFont* = nullptr) {}
  //! the text is wider than any control of the test: it scrolls
  bool Update(const std::string& text, float = 0, bool = false, bool = false)
  {
    const bool changed = text != m_text;
    m_text = text;
    return changed;
  }
  void GetTextExtent(float& width, float& height) const
  {
    width = 10000.0f;
    height = 20.0f;
  }
  void Render(float, float, float, uint32_t, uint32_t, uint32_t, float, bool = false) {}
  void RenderScrolling(float, float, float, uint32_t, uint32_t, uint32_t, float, const CScrollInfo&) {}
  void UpdateScrollinfo(CScrollInfo&) {}

private:
  std::string m_text;
};

class CLabelInfo
{
public:
  bool UpdateColors() { return false; }
  KODI::GUILIB::GUIINFO::CGUIInfoColor textColor;
  KODI::GUILIB::GUIINFO::CGUIInfoColor shadowColor;
  CGUIFont* font{nullptr};
  uint32_t align{XBFONT_LEFT};
  float offsetX{0};
  float offsetY{0};
  float angle{0};
  int scrollSpeed{60};
};

// --- VisibleEffect.h (CAnimation): a fader that is never in process
enum ANIMATION_PROCESS
{
  ANIM_PROCESS_NONE,
  ANIM_PROCESS_NORMAL,
  ANIM_PROCESS_REVERSE
};
enum ANIMATION_STATE
{
  ANIM_STATE_NONE,
  ANIM_STATE_DELAYED,
  ANIM_STATE_IN_PROCESS,
  ANIM_STATE_APPLIED
};
class CAnimation
{
public:
  static CAnimation CreateFader(float, float, unsigned int, unsigned int) { return {}; }
  void QueueAnimation(ANIMATION_PROCESS) {}
  void ApplyAnimation() {}
  void Animate(unsigned int, bool) {}
  void RenderAnimation(TransformMatrix&) {}
  ANIMATION_PROCESS GetProcess() const { return ANIM_PROCESS_NONE; }
  ANIMATION_STATE GetState() const { return ANIM_STATE_NONE; }
  void ResetAnimation() {}
};

// --- GraphicContext.h, WinSystem.h, ServiceBroker.h
enum RENDER_ORDER
{
  RENDER_ORDER_ALL_BACK_TO_FRONT,
  RENDER_ORDER_BACK_TO_FRONT,
  RENDER_ORDER_FRONT_TO_BACK
};
class CGraphicContext
{
public:
  TransformMatrix AddTransform(const TransformMatrix& matrix) { return matrix; }
  void SetTransform(const TransformMatrix& matrix) { m_renderedDepths.push_back(matrix.depth); }
  void RemoveTransform() {}
  RENDER_ORDER GetRenderOrder() const { return RENDER_ORDER_BACK_TO_FRONT; }
  uint32_t GetDepth() const { return m_depth; }

  uint32_t m_depth{0}; //!< the depth that this pass assigns (set by the test)
  std::vector<uint32_t> m_renderedDepths; //!< the depth of each SetTransform() of a Render()
};
class CWinSystemBase
{
public:
  CGraphicContext& GetGfxContext() { return m_gfx; }
  CGraphicContext m_gfx;
};
class CServiceBroker
{
public:
  static CWinSystemBase* GetWinSystem();
};

// --- GUIMessage.h
constexpr int GUI_MSG_LABEL_ADD = 1;
constexpr int GUI_MSG_LABEL_SET = 2;
constexpr int GUI_MSG_LABEL_RESET = 3;
class CGUIMessage
{
public:
  int GetControlId() const { return 0; }
  int GetMessage() const { return 0; }
  std::string GetLabel() const { return {}; }
};

// --- utils/Random.h
namespace KODI::UTILS
{
template<class It>
void RandomShuffle(It, It)
{
}
} // namespace KODI::UTILS
