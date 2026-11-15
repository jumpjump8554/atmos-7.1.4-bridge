#include "PluginEditor.h"

using namespace ab;

//==============================================================================
// Theme presets
//==============================================================================
struct Theme
{
    ThemeId id;
    juce::String menuName;
    bool dark;

    // Page and panels
    juce::Colour bgTop, bgMid, bgBottom;
    bool grid, scanlines;
    juce::Colour panel, panelBorder;
    float radius;
    bool chamfer, shadow;
    juce::Colour shadowColour;
    juce::Colour headerEnd, headerBorder, footerEnd, footerBorder;   // header/footer gradient (left = panel)

    // Text
    juce::Colour text, sub, dim, heading;
    bool headingGlow;

    // Controls
    juce::Colour btn, btnBorder, field;
    juce::Colour neon;             // levels, speaker glow, beams
    juce::Colour accent;           // "on" state of buttons
    bool solidActive;              // true: accent fill + activeText; false: accent tint + accent text
    juce::Colour activeText;
    juce::Colour clip, warnBg, warnBorder;
    juce::Colour muteBg, muteFg, muteBorder;
    juce::Colour bypBg, bypFg, bypBorder;
    juce::Colour ok, amber, red;
    juce::Colour pillBg, pillBorder;
    juce::Colour divider, rowLine;
    juce::Colour themeBoxText, themeBoxBorder, themeBoxBg;
    juce::Colour slotOffBg, slotOffBorder;

    // Title
    juce::Colour titleA, titleB, titleHighlight;

    // Room
    int roomStyle;                 // 0 = plain, 1 = wireframe (cyberpunk), 2 = beach
    juce::Colour roomBg, ceilA, ceilB, wallOuter, wallInner, backA, backB, floorA, floorB;
    juce::Colour screenBg, screenBorder, screenText;
    juce::Colour seatBg, seatBg2, seatBorder, seatText;
    juce::Colour spkBody, spkQuiet, numActive, numQuiet, labelActive, labelQuiet;
};

namespace
{
juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
juce::Colour C (juce::uint32 argb) { return juce::Colour (argb); }

Theme makeCyberpunk()
{
    Theme t {};
    t.id = ThemeId::Cyberpunk; t.menuName = tr ("테마: Cyberpunk"); t.dark = true;
    t.bgTop = t.bgMid = t.bgBottom = C (0xff07060d);
    t.grid = true; t.scanlines = true;
    t.panel = C (0xff0e0c18); t.panelBorder = C (0xff00f0ff).withAlpha (0.35f);
    t.radius = 0.0f; t.chamfer = true; t.shadow = false; t.shadowColour = juce::Colours::transparentBlack;
    t.headerEnd = t.panel; t.headerBorder = t.panelBorder; t.footerEnd = t.panel; t.footerBorder = t.panelBorder;
    t.text = C (0xffe9f7ff); t.sub = C (0xff8f8bb8); t.dim = C (0xffb9b5e6); t.heading = C (0xff00f0ff); t.headingGlow = true;
    t.btn = C (0xff14112a); t.btnBorder = C (0xff00f0ff).withAlpha (0.4f); t.field = C (0xff06050b);
    t.neon = C (0xff39ff14); t.accent = C (0xffff2bd6); t.solidActive = false; t.activeText = t.accent;
    t.clip = C (0xfffcee0a); t.warnBg = C (0xff22200a); t.warnBorder = C (0xff6b650a);
    t.muteBg = C (0xff5a1f1f); t.muteFg = C (0xffffc9c9); t.muteBorder = C (0xffb04040);
    t.bypBg = C (0xff2e2b06); t.bypFg = C (0xfffcee0a); t.bypBorder = C (0xfffcee0a);
    t.ok = t.neon; t.amber = C (0xfffcee0a); t.red = C (0xffff6b6b);
    t.pillBg = C (0xff151230); t.pillBorder = C (0xffff2bd6).withAlpha (0.4f);
    t.divider = C (0xff00f0ff).withAlpha (0.25f); t.rowLine = C (0xff00f0ff).withAlpha (0.10f);
    t.themeBoxText = C (0xffff2bd6); t.themeBoxBorder = C (0xffff2bd6).withAlpha (0.6f); t.themeBoxBg = t.field;
    t.slotOffBg = C (0xff07060d); t.slotOffBorder = C (0xff00f0ff).withAlpha (0.35f);
    t.titleA = C (0xff00f0ff); t.titleB = C (0xffff2bd6); t.titleHighlight = juce::Colours::transparentBlack;
    t.roomStyle = 1;
    t.roomBg = C (0xff0d0a1c); t.ceilA = C (0xff2a2150); t.ceilB = C (0xff1f1940);
    t.wallOuter = C (0xff1a1538); t.wallInner = C (0xff241d4a); t.backA = t.backB = C (0xff221c46);
    t.floorA = C (0xff1c1740); t.floorB = C (0xff2a2156);
    t.screenBg = C (0xff241e52); t.screenBorder = C (0xff00f0ff).withAlpha (0.7f); t.screenText = C (0xff7d78b0);
    t.seatBg = t.seatBg2 = C (0xff2a2356); t.seatBorder = C (0xffff2bd6).withAlpha (0.6f); t.seatText = t.sub;
    t.spkBody = C (0xff0c0f0d); t.spkQuiet = t.neon.withAlpha (0.30f);
    t.numActive = C (0xfff2fff0); t.numQuiet = t.sub; t.labelActive = t.text; t.labelQuiet = t.sub;
    return t;
}

Theme makeWhiteModern()
{
    Theme t {};
    t.id = ThemeId::WhiteModern; t.menuName = tr ("테마: White Modern"); t.dark = false;
    t.bgTop = t.bgMid = t.bgBottom = C (0xfff4f5f7);
    t.grid = false; t.scanlines = false;
    t.panel = C (0xffffffff); t.panelBorder = C (0xffe3e6ea);
    t.radius = 14.0f; t.chamfer = false; t.shadow = true; t.shadowColour = C (0xff101828).withAlpha (0.08f);
    t.headerEnd = t.panel; t.headerBorder = t.panelBorder; t.footerEnd = t.panel; t.footerBorder = t.panelBorder;
    t.text = C (0xff1a1d21); t.sub = C (0xff6b7280); t.dim = C (0xff4b5563); t.heading = t.text; t.headingGlow = false;
    t.btn = C (0xffffffff); t.btnBorder = C (0xffd9dde3); t.field = C (0xfff1f3f5);
    t.neon = C (0xffffb800); t.accent = t.neon; t.solidActive = true; t.activeText = C (0xff111316);
    t.clip = C (0xffe5484d); t.warnBg = C (0xfffdecec); t.warnBorder = C (0xfff5b5b7);
    t.muteBg = C (0xfffde8e8); t.muteFg = C (0xffb42318); t.muteBorder = C (0xfff3a6a6);
    t.bypBg = C (0xfffff4e0); t.bypFg = C (0xffb45309); t.bypBorder = C (0xfff59e0b);
    t.ok = C (0xff16a34a); t.amber = C (0xffd97706); t.red = C (0xffe5484d);
    t.pillBg = C (0xfff1f3f5); t.pillBorder = C (0xffe3e6ea);
    t.divider = C (0xffe3e6ea); t.rowLine = C (0xffeef0f3);
    t.themeBoxText = t.text; t.themeBoxBorder = t.btnBorder; t.themeBoxBg = C (0xffffffff);
    t.slotOffBg = C (0xfff4f5f7); t.slotOffBorder = C (0xffe3e6ea);
    t.titleA = C (0xff111316); t.titleB = C (0xff111316); t.titleHighlight = C (0xffffb800).withAlpha (0.55f);
    t.roomStyle = 0;
    t.roomBg = C (0xfff1f3f5); t.ceilA = C (0xffe9ecf0); t.ceilB = C (0xfff3f4f6);
    t.wallOuter = C (0xffe2e5ea); t.wallInner = C (0xffedeff2); t.backA = t.backB = C (0xfff6f7f9);
    t.floorA = C (0xffe6e9ed); t.floorB = C (0xffd9dde3);
    t.screenBg = C (0xfff1f3f5); t.screenBorder = C (0xffe3e6ea); t.screenText = t.sub;
    t.seatBg = t.seatBg2 = C (0xfff1f3f5); t.seatBorder = C (0xffe3e6ea); t.seatText = t.sub;
    t.spkBody = C (0xffffffff); t.spkQuiet = C (0xffc9ced6);
    t.numActive = C (0xff111316); t.numQuiet = t.sub; t.labelActive = C (0xff111316); t.labelQuiet = t.sub;
    return t;
}

Theme makeCoolBeaches()
{
    Theme t {};
    t.id = ThemeId::CoolBeaches; t.menuName = tr ("테마: Cool Beaches"); t.dark = false;
    t.bgTop = C (0xffe2f2f7); t.bgMid = C (0xffeef4f1); t.bgBottom = C (0xfff5ecdd);
    t.grid = false; t.scanlines = false;
    t.panel = C (0xfffffdf9); t.panelBorder = C (0xffe7dfd1);
    t.radius = 16.0f; t.chamfer = false; t.shadow = true; t.shadowColour = C (0xff0c3b4d).withAlpha (0.09f);
    t.headerEnd = C (0xffeaf7fb); t.headerBorder = C (0xffd9e9ee); t.footerEnd = C (0xfffdf0dc); t.footerBorder = C (0xffeadfcd);
    t.text = C (0xff0e2f44); t.sub = C (0xff5e7480); t.dim = C (0xff3f5a68); t.heading = t.text; t.headingGlow = false;
    t.btn = C (0xfffffdf9); t.btnBorder = C (0xffe2d6c2); t.field = C (0xfff6f1e8);
    t.neon = C (0xff0a84ff); t.accent = t.neon; t.solidActive = true; t.activeText = C (0xffffffff);
    t.clip = C (0xffe5483d); t.warnBg = C (0xffffede8); t.warnBorder = C (0xfff8c1b5);
    t.muteBg = C (0xffffe6e1); t.muteFg = C (0xffc0392b); t.muteBorder = C (0xfff5b0a3);
    t.bypBg = C (0xfffff1dc); t.bypFg = C (0xffb45309); t.bypBorder = C (0xfff7a64a);
    t.ok = C (0xff0f9d8a); t.amber = C (0xffd97706); t.red = C (0xffe5483d);
    t.pillBg = C (0xfff6f1e8); t.pillBorder = C (0xffe7dfd1);
    t.divider = C (0xffe7dfd1); t.rowLine = C (0xffefe7da);
    t.themeBoxText = t.text; t.themeBoxBorder = t.btnBorder; t.themeBoxBg = C (0xfffffdf9);
    t.slotOffBg = C (0xfffaf6ef); t.slotOffBorder = C (0xffe7dfd1);
    t.titleA = C (0xff0b2e44); t.titleB = C (0xff0b2e44); t.titleHighlight = C (0xffffa666).withAlpha (0.5f);
    t.roomStyle = 2;
    t.roomBg = C (0xfff6f1e8); t.ceilA = C (0xffd8eef8); t.ceilB = C (0xffeaf6fa);
    t.wallOuter = C (0xffcfe9f0); t.wallInner = C (0xffe4f3f6); t.backA = C (0xffeef8fc); t.backB = C (0xffdcf0f7);
    t.floorA = C (0xfff4e8d2); t.floorB = C (0xffead5b2);
    t.screenBg = juce::Colours::white.withAlpha (0.78f); t.screenBorder = C (0xffcfe3ea); t.screenText = t.sub;
    t.seatBg = C (0xfffff8ec); t.seatBg2 = C (0xffffe9cf); t.seatBorder = C (0xffecd6b4); t.seatText = C (0xff7a5a35);
    t.spkBody = C (0xffffffff); t.spkQuiet = C (0xffb9d3dc);
    t.numActive = C (0xff0b2e44); t.numQuiet = t.sub; t.labelActive = C (0xff0b2e44); t.labelQuiet = C (0xff3f5a68);
    return t;
}

const Theme& themeFor (ThemeId id)
{
    static const Theme themes[kNumThemes] = { makeCyberpunk(), makeWhiteModern(), makeCoolBeaches() };
    return themes[juce::jlimit (0, kNumThemes - 1, (int) id)];
}

//==============================================================================
juce::Font uiFont (float height, bool bold = false)
{
   #if JUCE_WINDOWS
    const juce::String name ("Malgun Gothic");   // Korean UI font included with Windows
   #elif defined (AB_UI_FONT)
    const juce::String name (AB_UI_FONT);
   #else
    const juce::String name (juce::Font::getDefaultSansSerifFontName());
   #endif
    return juce::Font (juce::FontOptions (name, height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float height, bool bold = false)
{
   #if JUCE_WINDOWS
    const juce::String name ("Consolas");
   #else
    const juce::String name (juce::Font::getDefaultMonospacedFontName());
   #endif
    return juce::Font (juce::FontOptions (name, height, bold ? juce::Font::bold : juce::Font::plain));
}

// Latin-only display text (title)
juce::Font titleFont (const Theme& t, float height)
{
   #if JUCE_WINDOWS
    const juce::String name (t.id == ThemeId::Cyberpunk ? "Bahnschrift" : "Segoe UI");
   #else
    const juce::String name (juce::Font::getDefaultSansSerifFontName());
   #endif
    auto f = juce::Font (juce::FontOptions (name, height, juce::Font::bold));
    if (t.id == ThemeId::Cyberpunk) f = f.withExtraKerningFactor (0.10f);
    else                            f = f.withExtraKerningFactor (-0.01f);
    return f;
}

// RMS (linear) -> 0..1 display value over a 60 dB range.
float displayLevel (float rms)
{
    if (rms <= 1.0e-6f) return 0.0f;
    const float db = juce::Decibels::gainToDecibels (rms, -100.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

juce::String dbText (float rms)
{
    if (rms < 1.0e-5f) return tr ("-\xe2\x88\x9e");
    return juce::String (juce::Decibels::gainToDecibels (rms), 1);
}

void setBorder (juce::Component& c, juce::Colour b) { c.getProperties().set ("border", (juce::int64) b.getARGB()); }

// Button colours for an "on/off" button in the current theme.
void styleToggle (juce::TextButton& b, const Theme& t, bool on)
{
    if (on)
    {
        b.setColour (juce::TextButton::buttonColourId, t.solidActive ? t.accent : t.accent.withAlpha (0.16f));
        b.setColour (juce::TextButton::textColourOffId, t.activeText);
        setBorder (b, t.accent);
    }
    else
    {
        b.setColour (juce::TextButton::buttonColourId, t.btn);
        b.setColour (juce::TextButton::textColourOffId, t.dim);
        setBorder (b, t.btnBorder);
    }
}

void stylePlain (juce::TextButton& b, const Theme& t)
{
    b.setColour (juce::TextButton::buttonColourId, t.btn);
    b.setColour (juce::TextButton::textColourOffId, t.text);
    setBorder (b, t.btnBorder);
}

juce::Path polygon (std::initializer_list<juce::Point<float>> pts)
{
    juce::Path p;
    bool first = true;
    for (auto pt : pts) { if (first) p.startNewSubPath (pt); else p.lineTo (pt); first = false; }
    p.closeSubPath();
    return p;
}

// Panel outline: chamfered (top-left and bottom-right corners cut) or rounded.
juce::Path panelShape (juce::Rectangle<float> r, const Theme& t, float cut = 14.0f)
{
    if (t.chamfer)
        return polygon ({ { r.getX() + cut, r.getY() }, { r.getRight(), r.getY() }, { r.getRight(), r.getBottom() - cut },
                          { r.getRight() - cut, r.getBottom() }, { r.getX(), r.getBottom() }, { r.getX(), r.getY() + cut } });
    juce::Path p;
    p.addRoundedRectangle (r, t.radius);
    return p;
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t,
                juce::Colour endColour = {}, juce::Colour border = {})
{
    const auto shape = panelShape (r, t);
    if (t.shadow)
    {
        juce::DropShadow (t.shadowColour, 22, { 0, 6 }).drawForPath (g, shape);
        juce::DropShadow (t.shadowColour.withMultipliedAlpha (0.8f), 3, { 0, 1 }).drawForPath (g, shape);
    }
    if (endColour.getAlpha() > 0 && endColour != t.panel)
    {
        juce::ColourGradient grad (t.panel, r.getX(), 0.0f, endColour, r.getRight(), 0.0f, false);
        grad.addColour (0.4, t.panel);
        g.setGradientFill (grad);
    }
    else
    {
        g.setColour (t.panel);
    }
    g.fillPath (shape);
    g.setColour (border.getAlpha() > 0 ? border : t.panelBorder);
    g.strokePath (panelShape (r.reduced (0.5f), t), juce::PathStrokeType (1.0f));
}

// Text drawn as a path, with an optional glow behind it.
void drawTextPath (juce::Graphics& g, const juce::String& text, const juce::Font& f, juce::Rectangle<float> area,
               juce::Justification j, juce::Colour colour, juce::Colour glow = {}, int glowRadius = 0)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), j, 1, 1.0f);
    juce::Path p;
    ga.createPath (p);
    if (glowRadius > 0 && glow.getAlpha() > 0)
        juce::DropShadow (glow, glowRadius, {}).drawForPath (g, p);
    g.setColour (colour);
    g.fillPath (p);
}

// Multi-line text with real word wrapping (Korean text wraps at spaces).
void drawWrapped (juce::Graphics& g, const juce::String& text, const juce::Font& f, juce::Colour c, juce::Rectangle<float> area)
{
    juce::AttributedString as;
    as.setText (text);
    as.setFont (f);
    as.setColour (c);
    as.setWordWrap (juce::AttributedString::byWord);
    as.setJustification (juce::Justification::topLeft);
    as.setLineSpacing (2.0f);
    juce::TextLayout tl;
    tl.createLayout (as, area.getWidth());
    tl.draw (g, area);
}

void drawHeading (juce::Graphics& g, const Theme& t, const juce::String& text, juce::Rectangle<float> area, float size = 18.0f)
{
    drawTextPath (g, text, uiFont (size, true), area, juce::Justification::centredLeft, t.heading,
              t.headingGlow ? t.heading.withAlpha (0.55f) : juce::Colour(), t.headingGlow ? 8 : 0);
}

void drawSpeakerIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float stroke)
{
    const float s = r.getWidth() / 24.0f;
    auto P = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * s, r.getY() + y * s); };
    juce::Path p;
    p.startNewSubPath (P (4, 9)); p.lineTo (P (4, 15)); p.lineTo (P (8, 15)); p.lineTo (P (13, 19));
    p.lineTo (P (13, 5)); p.lineTo (P (8, 9)); p.closeSubPath();
    juce::Path w1; w1.addCentredArc (r.getX() + 12 * s, r.getY() + 12 * s, 4 * s, 4 * s, 0, 0.6f, 2.55f, true);
    juce::Path w2; w2.addCentredArc (r.getX() + 11 * s, r.getY() + 12 * s, 8 * s, 8 * s, 0, 0.75f, 2.4f, true);
    g.setColour (c);
    const juce::PathStrokeType st (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.strokePath (p, st); g.strokePath (w1, st); g.strokePath (w2, st);
}

//============================================================================== layout (RootView coordinates)
const juce::Rectangle<float> kHeader  { 20.0f,  20.0f, 1700.0f,  60.0f };
const juce::Rectangle<int>   kRoom    { 20,  96, 780, 620 };
const juce::Rectangle<int>   kMap     { 820, 96, 440, 620 };
const juce::Rectangle<int>   kRight   { 1280, 96, 440, 620 };
const juce::Rectangle<float> kFooter  { 20.0f, 732.0f, 1700.0f,  60.0f };
const juce::Rectangle<float> kStatusPill { 988.0f, 34.0f, 346.0f, 32.0f };
const juce::Rectangle<float> kRatePill   { 1346.0f, 34.0f, 124.0f, 32.0f };
const juce::Rectangle<float> kModeGroup  { 1482.0f, 32.0f, 218.0f, 36.0f };
const juce::Rectangle<float> kDropouts   { 1366.0f, 744.0f, 84.0f, 36.0f };

// MapPanel rows
constexpr int kRowY0 = 124, kRowH = 34;
} // namespace

//==============================================================================
class NeonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
       #if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Malgun Gothic");
       #endif
        setTheme (themeFor (ThemeId::Cyberpunk));
    }

    void setTheme (const Theme& th)
    {
        t = &th;
        setColour (juce::ComboBox::backgroundColourId, th.field);
        setColour (juce::ComboBox::textColourId, th.text);
        setColour (juce::ComboBox::outlineColourId, th.btnBorder);
        setColour (juce::ComboBox::arrowColourId, th.sub);
        setColour (juce::PopupMenu::backgroundColourId, th.panel);
        setColour (juce::PopupMenu::textColourId, th.text);
        setColour (juce::PopupMenu::headerTextColourId, th.sub);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, th.solidActive ? th.accent.withAlpha (0.22f) : th.neon.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, th.text);
        setColour (juce::TooltipWindow::backgroundColourId, th.panel);
        setColour (juce::TooltipWindow::textColourId, th.text);
        setColour (juce::TooltipWindow::outlineColourId, th.panelBorder);
        setColour (juce::TextButton::buttonColourId, th.btn);
        setColour (juce::TextButton::textColourOffId, th.text);
        setColour (juce::TextButton::textColourOnId, th.text);
        setColour (juce::ResizableWindow::backgroundColourId, th.bgTop);
        setColour (juce::DocumentWindow::backgroundColourId, th.bgTop);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = (float) (double) b.getProperties().getWithDefault ("radius", 7.0);
        auto fill = backgroundColour;
        if (fill.getAlpha() > 0 || over || down)
        {
            const bool light = fill.getPerceivedBrightness() > 0.6f;
            if (down)      fill = light ? fill.darker (0.10f) : fill.brighter (0.15f);
            else if (over) fill = light ? fill.darker (0.04f) : fill.brighter (0.07f);
        }
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        const auto bdr = b.getProperties().getWithDefault ("border", (juce::int64) t->btnBorder.getARGB());
        const auto bc = juce::Colour ((juce::uint32) (juce::int64) bdr);
        if (bc.getAlpha() > 0)
        {
            g.setColour (bc);
            g.drawRoundedRectangle (r, radius, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
    {
        const bool mono = (bool) b.getProperties().getWithDefault ("mono", false);
        const float h = juce::jmin (16.0f, (float) buttonHeight * 0.52f);
        return mono ? monoFont (h, true) : uiFont (h, (bool) b.getProperties().getWithDefault ("bold", false));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        if ((bool) box.getProperties().getWithDefault ("noArrow", false)) return;
        juce::Path p;
        const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
        p.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (p);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        const bool mono = (bool) box.getProperties().getWithDefault ("mono", false);
        return mono ? monoFont (16.0f) : uiFont (15.0f);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        const bool noArrow = (bool) box.getProperties().getWithDefault ("noArrow", false);
        label.setBounds (2, 1, box.getWidth() - (noArrow ? 4 : 22), box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (noArrow ? juce::Justification::centred : juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return uiFont (15.0f); }

private:
    const Theme* t = nullptr;
};

//==============================================================================
// Shared, per-editor UI context (current theme).
struct UiCtx
{
    const Theme* t = &themeFor (ThemeId::Cyberpunk);
    const Theme& th() const noexcept { return *t; }
};

//==============================================================================
// Pink-noise button with a small waveform icon.
class NoiseButton final : public juce::Button
{
public:
    explicit NoiseButton (const UiCtx& c) : juce::Button ("noise"), ctx (c) {}
    bool active = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = ctx.th();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        auto fill = active ? t.neon.withAlpha (t.dark ? 0.18f : 0.16f) : t.btn;
        if (down) fill = t.dark ? fill.brighter (0.15f) : fill.darker (0.08f);
        else if (over) fill = t.dark ? fill.brighter (0.07f) : fill.darker (0.03f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (active ? t.neon : t.btnBorder);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        // waveform: M1 7h2l1.5-5 2 10 2-8 1.5 6 1.5-4 1.5 3 1-2H17 in an 18x14 box
        const float ox = r.getCentreX() - 9.0f, oy = r.getCentreY() - 7.0f;
        juce::Path p;
        const float pts[][2] = { {1,7},{3,7},{4.5f,2},{6.5f,12},{8.5f,4},{10,10},{11.5f,6},{13,9},{14,7},{17,7} };
        p.startNewSubPath (ox + pts[0][0], oy + pts[0][1]);
        for (int i = 1; i < 10; ++i) p.lineTo (ox + pts[i][0], oy + pts[i][1]);
        g.setColour (active ? (t.dark ? t.neon : t.neon.darker (0.35f)) : t.dim);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    const UiCtx& ctx;
};

//==============================================================================
// Speaker geometry in the room drawing (room-local coordinates, 780 x 620).
struct SpeakerGeo { float x, y, w, h; bool ellipse; float radius; bool top; float bx, by, bw, bh; };

static const SpeakerGeo kGeo[kNumChannels] = {
    { 254, 300, 22, 60, false, 4, false, 0, 0, 0, 0 },   // L
    { 504, 300, 22, 60, false, 4, false, 0, 0, 0, 0 },   // R
    { 362, 330, 56, 20, false, 4, false, 0, 0, 0, 0 },   // C
    { 296, 328, 32, 32, false, 4, false, 0, 0, 0, 0 },   // LFE
    { 123, 500, 54, 62, false, 5, false, 0, 0, 0, 0 },   // SBL
    { 603, 500, 54, 62, false, 5, false, 0, 0, 0, 0 },   // SBR
    {  65, 390, 40, 46, false, 5, false, 0, 0, 0, 0 },   // SL
    { 675, 390, 40, 46, false, 5, false, 0, 0, 0, 0 },   // SR
    { 275, 122, 50, 16, true,  0, true, 240, 130, 120, 110 }, // TFL
    { 455, 122, 50, 16, true,  0, true, 420, 130, 120, 110 }, // TFR
    { 110,  33, 80, 24, true,  0, true,  65,  45, 170, 150 }, // TRL
    { 590,  33, 80, 24, true,  0, true, 545,  45, 170, 150 }, // TRR
};

//==============================================================================
// Static part of the room (walls, floor, screen, titles). Cached as an image; redrawn on theme change only.
class RoomStatic final : public juce::Component
{
public:
    explicit RoomStatic (const UiCtx& c) : ctx (c)
    {
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        const auto bounds = getLocalBounds().toFloat();
        const auto shape = panelShape (bounds, t);
        g.saveState();
        g.reduceClipRegion (shape);

        g.fillAll (t.roomBg);
        auto vgrad = [&] (juce::Colour a, float y0, juce::Colour b, float y1)
        { g.setGradientFill (juce::ColourGradient (a, 0.0f, y0, b, 0.0f, y1, false)); };
        auto hgrad = [&] (juce::Colour a, float x0, juce::Colour b, float x1)
        { g.setGradientFill (juce::ColourGradient (a, x0, 0.0f, b, x1, 0.0f, false)); };

        vgrad (t.ceilA, 0, t.ceilB, 170);        g.fillPath (polygon ({ {0,0}, {780,0}, {540,170}, {240,170} }));
        hgrad (t.wallOuter, 0, t.wallInner, 240); g.fillPath (polygon ({ {0,0}, {240,170}, {240,360}, {0,620} }));
        hgrad (t.wallOuter, 780, t.wallInner, 540); g.fillPath (polygon ({ {780,0}, {540,170}, {540,360}, {780,620} }));

        if (t.roomStyle == 2)
        {
            // Window to the sea: sky, horizon, sunset sun, water
            const float horizon = 170.0f + 190.0f * 0.765f;
            vgrad (t.backA, 170, t.backB, horizon); g.fillRect (240.0f, 170.0f, 300.0f, horizon - 170.0f);
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> (240, 170, 300, (int) (horizon - 170.0f)));
            g.setGradientFill (juce::ColourGradient (C (0xffffaa64).withAlpha (0.55f), 460.0f, horizon,
                                                     C (0xffffaa64).withAlpha (0.0f), 460.0f + 44.0f, horizon, true));
            g.fillEllipse (390.0f, horizon - 70.0f, 140.0f, 140.0f);
            g.setGradientFill (juce::ColourGradient (C (0xffffd08a), 0.0f, horizon - 16.0f, C (0xffff9466), 0.0f, horizon + 16.0f, false));
            g.fillEllipse (444.0f, horizon - 16.0f, 32.0f, 32.0f);
            g.restoreState();
            vgrad (C (0xff8fd0e0), horizon, C (0xffb5e2ea), 360); g.fillRect (240.0f, horizon, 300.0f, 360.0f - horizon);
        }
        else
        {
            vgrad (t.backA, 170, t.backB, 360); g.fillRect (240.0f, 170.0f, 300.0f, 190.0f);
        }
        vgrad (t.floorA, 360, t.floorB, 620); g.fillPath (polygon ({ {240,360}, {540,360}, {780,620}, {0,620} }));

        if (t.roomStyle == 1)
        {
            // Neon wireframe: magenta floor grid, cyan room edges
            g.setColour (C (0xffff2bd6).withAlpha (0.35f));
            const float floorLines[][4] = { {240,360,0,620}, {290,360,130,620}, {340,360,260,620}, {390,360,390,620},
                                            {440,360,520,620}, {490,360,650,620}, {540,360,780,620},
                                            {223.4f,378,556.6f,378}, {201.2f,402,578.8f,402}, {171.7f,434,608.3f,434},
                                            {131.1f,478,648.9f,478}, {73.8f,540,706.2f,540} };
            for (auto& l : floorLines) g.drawLine (l[0], l[1], l[2], l[3], 1.0f);

            juce::Path edges;
            edges.startNewSubPath (0, 0);     edges.lineTo (240, 170);
            edges.startNewSubPath (780, 0);   edges.lineTo (540, 170);
            edges.startNewSubPath (0, 620);   edges.lineTo (240, 360);
            edges.startNewSubPath (780, 620); edges.lineTo (540, 360);
            edges.addRectangle (240, 170, 300, 190);
            juce::Path stroked;
            juce::PathStrokeType (1.5f).createStrokedPath (stroked, edges);
            juce::DropShadow (C (0xff00f0ff).withAlpha (0.8f), 6, {}).drawForPath (g, stroked);
            g.setColour (C (0xff00f0ff).withAlpha (0.75f));
            g.fillPath (stroked);
        }
        else if (t.roomStyle == 2)
        {
            // Foam line where the sea meets the sand
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            juce::DropShadow (juce::Colours::white.withAlpha (0.9f), 6, {}).drawForPath (g, [] { juce::Path p; p.addRoundedRectangle (240.0f, 358.0f, 300.0f, 4.0f, 2.0f); return p; }());
            g.fillRoundedRectangle (240.0f, 358.0f, 300.0f, 4.0f, 2.0f);
        }

        // Title
        drawTextPath (g, tr ("7.1.4 스피커 배치"), uiFont (17.0f, true), { 0, 10, 780, 22 }, juce::Justification::centred,
                  t.dark ? t.heading : t.text, t.headingGlow ? t.heading.withAlpha (0.5f) : juce::Colour(), t.headingGlow ? 8 : 0);
        g.setColour (t.sub);
        g.setFont (uiFont (15.5f));
        g.drawText (tr ("스피커를 클릭하면 해당 위치로 핑크 노이즈(-20 dBFS) 출력"), 0, 32, 780, 18, juce::Justification::centred);

        // Screen
        const juce::Rectangle<float> screen (270, 190, 240, 100);
        if (t.roomStyle == 1)
            juce::DropShadow (C (0xff00f0ff).withAlpha (0.35f), 18, {}).drawForPath (g, [&] { juce::Path p; p.addRectangle (screen); return p; }());
        g.setColour (t.screenBg); g.fillRoundedRectangle (screen, 3);
        g.setColour (t.screenBorder); g.drawRoundedRectangle (screen, 3, 1);
        g.setColour (t.screenText); g.setFont (monoFont (14.0f));
        g.drawText ("S C R E E N", screen.toNearestInt(), juce::Justification::centred);

        // Listening position
        const juce::Rectangle<float> seat (280, 448, 220, 54);
        if (t.chamfer)
        {
            const auto p = panelShape (seat, t);
            g.setColour (t.seatBg); g.fillPath (p);
            g.setColour (t.seatBorder); g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        else
        {
            if (t.shadow)
                juce::DropShadow (t.roomStyle == 2 ? C (0xff78501e).withAlpha (0.12f) : t.shadowColour, 16, { 0, 5 })
                    .drawForPath (g, [&] { juce::Path p; p.addRoundedRectangle (seat, 14.0f); return p; }());
            g.saveState();
            g.reduceClipRegion ([&] { juce::Path p; p.addRoundedRectangle (seat, 14.0f); return p; }());
            g.setColour (t.seatBg); g.fillRect (seat);
            if (t.seatBg2 != t.seatBg)  // beach towel stripes
            {
                g.setColour (t.seatBg2);
                for (float x = seat.getX() + 16.0f; x < seat.getRight(); x += 32.0f)
                    g.fillRect (x, seat.getY(), 16.0f, seat.getHeight());
            }
            g.restoreState();
            g.setColour (t.seatBorder); g.drawRoundedRectangle (seat, 14.0f, 1.0f);
        }
        g.setColour (t.seatText); g.setFont (uiFont (14.5f, t.roomStyle == 2));
        g.drawText (tr ("청취 위치"), seat.toNearestInt(), juce::Justification::centred);

        g.restoreState();
        g.setColour (t.panelBorder);
        g.strokePath (panelShape (bounds.reduced (0.5f), t), juce::PathStrokeType (1.0f));
    }

private:
    const UiCtx& ctx;
};

//==============================================================================
// Live part of the room: beams and speakers with their neon level.
class RoomView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    RoomView (AtmosBridgeProcessor& p, const UiCtx& c) : proc (p), ctx (c), staticPart (c)
    {
        addAndMakeVisible (staticPart);
        setComponentID ("room");
        setTooltip (tr ("스피커를 클릭하면 해당 위치로 핑크 노이즈(-20 dBFS)가 나갑니다. 다시 클릭하면 멈춥니다."));
    }

    void themeChanged() { staticPart.repaint(); repaint(); }
    void resized() override { staticPart.setBounds (getLocalBounds()); }

    void refresh()
    {
        for (int p = 0; p < kNumChannels; ++p)
        {
            const auto m = proc.takePositionMeter (p);
            const float target = displayLevel (m.rms);
            level[(size_t) p] = juce::jmax (target, level[(size_t) p] - 0.035f);
            if (m.peak >= 0.98f) clipHold[(size_t) p] = 12;
            else if (clipHold[(size_t) p] > 0) --clipHold[(size_t) p];
        }
        repaint();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        g.reduceClipRegion (panelShape (getLocalBounds().toFloat().reduced (1.0f), t));

        // Which inputs feed each position
        juce::String numbers[kNumChannels];
        for (int i = 0; i < kNumChannels; ++i)
        {
            const int p = proc.getPosition (i);
            if (p >= 0) numbers[p] += (numbers[p].isEmpty() ? "" : ",") + juce::String (i + 1);
        }

        // Ceiling beams (behind the speakers)
        for (int p = 0; p < kNumChannels; ++p)
        {
            const auto& s = kGeo[p];
            if (! s.top) continue;
            const float l = juce::jmin (1.0f, level[(size_t) p] * 1.2f);
            if (l <= 0.01f) continue;
            juce::Path beam;
            beam.startNewSubPath (s.bx + s.bw * 0.42f, s.by);
            beam.lineTo (s.bx + s.bw * 0.58f, s.by);
            beam.lineTo (s.bx + s.bw, s.by + s.bh);
            beam.lineTo (s.bx, s.by + s.bh);
            beam.closeSubPath();
            juce::ColourGradient grad (t.neon.withAlpha (0.55f * l), s.bx, s.by, t.neon.withAlpha (0.0f), s.bx, s.by + s.bh, false);
            g.setGradientFill (grad);
            g.fillPath (beam);
        }

        const int seq = proc.getSequencePosition();
        const int posNoise = seq >= 0 ? seq : proc.getPositionNoise();
        for (int p = 0; p < kNumChannels; ++p)
            drawSpeaker (g, t, p, level[(size_t) p], clipHold[(size_t) p] > 0, p == posNoise,
                         numbers[p].isEmpty() ? tr ("\xe2\x80\x93") : numbers[p]);

        if (proc.isBypassed())
        {
            // Veil + label: the Atmos output is paused, the original goes to the host output.
            g.setColour ((t.dark ? juce::Colours::black : juce::Colours::white).withAlpha (t.dark ? 0.45f : 0.55f));
            g.fillAll();
            const auto tag = kBypassTag;
            g.setColour (tagHover ? (t.dark ? t.bypBorder.brighter (0.25f) : t.bypBorder.darker (0.12f)) : t.bypBorder);
            g.fillRoundedRectangle (tag, 10.0f);
            const auto fg = t.dark ? C (0xff1a1600) : t.titleA;
            drawTextPath (g, "BYPASS", uiFont (22.0f, true), tag.withHeight (36.0f).translated (0, 4), juce::Justification::centred, fg);
            g.setColour (fg);
            g.setFont (uiFont (13.5f));
            g.drawText (tr ("Atmos 출력 정지 \xc2\xb7 원본은 호스트 출력으로"), tag.withTrimmedTop (34.0f).toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (proc.isBypassed())
        {
            // While bypassed only the BYPASS label reacts: clicking it turns BYPASS off.
            const bool over = kBypassTag.contains (e.position);
            if (over != tagHover) { tagHover = over; repaint(); }
            setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            setTooltip (over ? tr ("클릭하면 BYPASS 해제") : juce::String());
            return;
        }
        setTooltip (tr ("스피커를 클릭하면 해당 위치로 핑크 노이즈(-20 dBFS)가 나갑니다. 다시 클릭하면 멈춥니다."));
        setMouseCursor (hitSpeaker (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (tagHover) { tagHover = false; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (proc.isBypassed())
        {
            if (kBypassTag.contains (e.position))
            {
                proc.setBypassed (false);
                tagHover = false;
                setMouseCursor (juce::MouseCursor::NormalCursor);
                repaint();
            }
            return; // speaker test tones are silent while bypassed, so speaker clicks do nothing
        }
        const int p = hitSpeaker (e.position);
        if (p >= 0) proc.togglePositionNoise (p);
    }

private:
    inline static const juce::Rectangle<float> kBypassTag { 250.0f, 206.0f, 280.0f, 66.0f }; // over the screen
    bool tagHover = false;

    int hitSpeaker (juce::Point<float> pt) const
    {
        for (int p = 0; p < kNumChannels; ++p)
        {
            const auto& s = kGeo[p];
            if (juce::Rectangle<float> (s.x, s.y, s.w, s.h + 18.0f).expanded (8.0f).contains (pt)) return p;
        }
        return -1;
    }

    static void drawSpeaker (juce::Graphics& g, const Theme& t, int p, float l, bool clip, bool testing, const juce::String& number)
    {
        const auto& s = kGeo[p];
        const juce::Rectangle<float> r (s.x, s.y, s.w, s.h);
        juce::Path shape;
        if (s.ellipse) shape.addEllipse (r);
        else           shape.addRoundedRectangle (r, s.radius);

        // Neon: 20 % brighter and larger than v1.1
        const auto glow = clip ? t.clip : t.neon;
        if (l > 0.02f)
        {
            juce::DropShadow (glow.withAlpha (juce::jlimit (0.0f, 1.0f, (0.15f + 0.8f * l) * 1.2f)),
                              (int) ((4.0f + 26.0f * l) * 1.2f), {}).drawForPath (g, shape);
        }
        if (! t.dark)
            juce::DropShadow (C (0xff101828).withAlpha (0.10f), 3, { 0, 1 }).drawForPath (g, shape);
        g.setColour (t.spkBody);
        g.fillPath (shape);
        if (l > 0.02f)
        {
            g.setColour (glow.withAlpha (juce::jmin (1.0f, (t.dark ? 0.05f + 0.22f * l : 0.03f + 0.14f * l) * 1.2f)));
            g.fillPath (shape);
        }

        juce::Colour border;
        if (clip)        border = t.clip;
        else if (t.dark) border = t.neon.withAlpha (juce::jlimit (0.0f, 1.0f, (0.25f + 0.75f * l) * 1.2f));
        else             border = l > 0.05f ? t.neon.withAlpha (juce::jlimit (0.0f, 1.0f, (0.55f + 0.45f * l) * 1.2f)) : t.spkQuiet;
        g.setColour (border);
        g.strokePath (shape, juce::PathStrokeType (testing ? 2.6f : 2.0f));

        // Input channel number(s) inside the speaker
        const float fh = s.h < 20.0f ? 13.0f : (s.w >= 40.0f ? 18.0f : 14.0f);
        const auto numRect = r.expanded (14.0f, 0.0f).toNearestInt();
        g.setFont (monoFont (fh, true));
        if (t.dark && l > 0.05f && ! clip)
        {
            g.setColour (t.neon.withAlpha (0.35f * l));
            for (auto d : { juce::Point<int> (1, 0), juce::Point<int> (-1, 0), juce::Point<int> (0, 1), juce::Point<int> (0, -1) })
                g.drawText (number, numRect + d, juce::Justification::centred, false);
        }
        g.setColour (clip ? t.clip : (l > 0.05f ? t.numActive : t.numQuiet));
        g.drawText (number, numRect, juce::Justification::centred, false);

        // Position code below
        g.setFont (monoFont (13.0f, true));
        g.setColour (l > 0.05f ? t.labelActive : t.labelQuiet);
        g.drawText (kPosCodes[(size_t) p], juce::Rectangle<float> (r.getCentreX() - 40.0f, r.getBottom() + 4.0f, 80.0f, 16.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }

    AtmosBridgeProcessor& proc;
    const UiCtx& ctx;
    RoomStatic staticPart;
    std::array<float, kNumChannels> level {};
    std::array<int, kNumChannels> clipHold {};
};

//==============================================================================
// Window showing a hosted upmixer's own editor.
class HostedPluginWindow final : public juce::DocumentWindow
{
public:
    HostedPluginWindow (juce::AudioPluginInstance& p, juce::Colour bg, std::function<void()> onCloseRequested)
        : juce::DocumentWindow (p.getName() + " (Atmos Bridge)", bg, juce::DocumentWindow::closeButton),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* ed = p.createEditorAndMakeActive();
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (p);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        centreWithSize (juce::jmax (200, getWidth()), juce::jmax (100, getHeight()));
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};

//==============================================================================
class MapPanel final : public juce::Component
{
public:
    MapPanel (AtmosBridgeProcessor& p, const UiCtx& c) : proc (p), ctx (c)
    {
        for (int i = 0; i < kNumChannels; ++i)
        {
            auto& cb = numBox[(size_t) i];
            cb.getProperties().set ("mono", true);
            cb.getProperties().set ("noArrow", true);
            for (int v = 0; v <= kNumChannels; ++v) cb.addItem (juce::String (v), v + 1);
            cb.setTooltip (tr ("출력 위치 번호 (1=L 2=R 3=C 4=LFE 5=SBL 6=SBR 7=SL 8=SR 9=TFL 10=TFR 11=TRL 12=TRR, 0=끄기)"));
            cb.onChange = [this, i]
            {
                const int id = numBox[(size_t) i].getSelectedId();
                if (id > 0) proc.setPosition (i, id - 2);
            };
            addAndMakeVisible (cb);

            noiseBtn[(size_t) i] = std::make_unique<NoiseButton> (ctx);
            auto& nb = *noiseBtn[(size_t) i];
            nb.setTooltip (tr ("Ch ") + juce::String (i + 1) + tr (" 핑크 노이즈 (-20 dBFS, 매핑·뮤트 적용)"));
            nb.onClick = [this, i] { proc.toggleInputNoise (i); };
            addAndMakeVisible (nb);

            auto& mb = muteBtn[(size_t) i];
            mb.setButtonText ("M");
            mb.getProperties().set ("mono", true);
            mb.getProperties().set ("radius", 6.0);
            mb.setTooltip (tr ("Ch ") + juce::String (i + 1) + tr (" 뮤트"));
            mb.onClick = [this, i] { proc.setMuted (i, ! proc.isMuted (i)); };
            addAndMakeVisible (mb);
        }

        resetBtn.setButtonText (tr ("기본값"));
        resetBtn.setTooltip (tr ("지금 쓰는 번호표를 기본 순서(1~12)로 되돌립니다"));
        resetBtn.onClick = [this] { proc.resetMapping(); };
        addAndMakeVisible (resetBtn);

        allMuteBtn.onClick = [this] { proc.setAllMuted (! proc.areAllMuted()); };
        addAndMakeVisible (allMuteBtn);
    }

    void resized() override
    {
        resetBtn.setBounds (254, 18, 64, 30);
        allMuteBtn.setBounds (326, 18, 96, 30);
        for (int i = 0; i < kNumChannels; ++i)
        {
            const int y = kRowY0 + i * kRowH;
            numBox[(size_t) i].setBounds (76, y + 3, 52, 28);
            noiseBtn[(size_t) i]->setBounds (230, y + 3, 30, 28);
            muteBtn[(size_t) i].setBounds (272, y + 3, 30, 28);
        }
    }

    void refresh()
    {
        const auto& t = ctx.th();
        for (int i = 0; i < kNumChannels; ++i)
        {
            const auto m = proc.takeInputMeter (i);
            rms[(size_t) i] = juce::jmax (m.rms, rms[(size_t) i] * 0.85f);

            auto& cb = numBox[(size_t) i];
            const int wantId = proc.getPosition (i) + 2;
            if (cb.getSelectedId() != wantId && ! cb.isPopupActive())
                cb.setSelectedId (wantId, juce::dontSendNotification);
            cb.setColour (juce::ComboBox::outlineColourId,
                          proc.isDuplicatePosition (proc.getPosition (i)) ? t.clip : t.btnBorder);

            auto& nb = *noiseBtn[(size_t) i];
            const bool nOn = proc.getInputNoise() == i;
            if (nb.active != nOn) { nb.active = nOn; nb.repaint(); }

            auto& mb = muteBtn[(size_t) i];
            const bool muted = proc.isMuted (i);
            mb.setColour (juce::TextButton::buttonColourId, muted ? t.muteBg : t.btn);
            mb.setColour (juce::TextButton::textColourOffId, muted ? t.muteFg : t.dim);
            setBorder (mb, muted ? t.muteBorder : t.btnBorder);
        }

        const bool all = proc.areAllMuted();
        allMuteBtn.setButtonText (all ? "All Unmute" : "All Mute");
        allMuteBtn.setColour (juce::TextButton::buttonColourId, all ? t.muteBg : t.btn);
        allMuteBtn.setColour (juce::TextButton::textColourOffId, all ? t.muteFg : t.text);
        setBorder (allMuteBtn, all ? t.muteBorder : t.btnBorder);
        allMuteBtn.repaint();
        for (auto& mb : muteBtn) mb.repaint();
        repaint();
    }

    void themeChanged()
    {
        const auto& t = ctx.th();
        for (auto& cb : numBox)
        {
            cb.setColour (juce::ComboBox::backgroundColourId, t.field);
            cb.setColour (juce::ComboBox::textColourId, t.text);
        }
        stylePlain (resetBtn, t);
        for (auto& nb : noiseBtn) nb->repaint();
        refresh();
    }

    // Static texts are drawn by the cached background (see BackgroundView::paintMapStatic).
    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();

        bool anyDup = false;
        for (int i = 0; i < kNumChannels; ++i) anyDup = anyDup || proc.isDuplicatePosition (proc.getPosition (i));
        if (anyDup)
        {
            juce::Rectangle<float> banner (18, 78, 404, 24);
            g.setColour (t.warnBg); g.fillRoundedRectangle (banner, 6);
            g.setColour (t.warnBorder); g.drawRoundedRectangle (banner, 6, 1);
            g.setColour (t.clip); g.setFont (uiFont (14.5f));
            g.drawText (tr ("같은 번호가 중복되어 해당 위치에 합산됩니다."), banner.reduced (10, 0).toNearestInt(), juce::Justification::centredLeft);
        }

        for (int i = 0; i < kNumChannels; ++i)
        {
            const int y = kRowY0 + i * kRowH;
            const bool muted = proc.isMuted (i);
            const float alpha = muted ? 0.55f : 1.0f;
            g.setColour (t.rowLine);
            g.drawHorizontalLine (y, 18.0f, 422.0f);

            g.setColour (t.text.withMultipliedAlpha (alpha));
            g.setFont (monoFont (15.0f));
            g.drawText ("Ch " + juce::String (i + 1), 18, y, 56, kRowH, juce::Justification::centredLeft);

            const int pos = proc.getPosition (i);
            const bool dup = proc.isDuplicatePosition (pos);
            g.setFont (monoFont (15.0f, true));
            g.setColour ((pos < 0 ? t.sub : t.text).withMultipliedAlpha (alpha));
            const juce::String target = pos >= 0 ? juce::String (kPosCodes[(size_t) pos]) : tr ("끄기");
            g.drawText (target, 140, y, 44, kRowH, juce::Justification::centredLeft);
            if (dup)
            {
                juce::Rectangle<float> tag (184, (float) y + 10, 34, 15);
                g.setColour (t.warnBorder); g.drawRoundedRectangle (tag, 4, 1);
                g.setColour (t.clip); g.setFont (uiFont (12.0f));
                g.drawText (tr ("중복"), tag.toNearestInt(), juce::Justification::centred);
            }

            // Meter
            const float l = displayLevel (rms[(size_t) i]);
            juce::Rectangle<float> bar (312, (float) y + 13, 60, 8);
            g.setColour (t.field); g.fillRoundedRectangle (bar, 4);
            if (l > 0.0f)
            {
                const auto fill = bar.withWidth (juce::jmax (2.0f, 60.0f * l));
                if (t.dark) { g.setColour (t.neon.withAlpha (0.35f * alpha)); g.fillRoundedRectangle (fill.expanded (1.0f), 5); }
                g.setColour (t.neon.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (fill, 4);
            }
            g.setColour (t.sub);
            g.setFont (monoFont (13.0f));
            g.drawText (dbText (rms[(size_t) i]), 374, y, 48, kRowH, juce::Justification::centredRight);
        }

        const int ry = kRowY0 + kNumChannels * kRowH;
        g.setColour (t.rowLine);
        g.drawHorizontalLine (ry, 18.0f, 422.0f);

        // Which upmixer is applied (the number table shown belongs to it: each slot keeps its own)
        const int a = proc.getActiveSlot();
        const juce::String owner = a >= 0 ? proc.getSlot (a).name : tr ("업믹서 없음 (호스트 채널 1~12)");
        g.setFont (uiFont (14.0f));
        g.setColour (t.sub);
        const juce::String label = tr ("적용된 업믹서:");
        const int labelW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (uiFont (14.0f), label));
        g.drawText (label, 18, ry + 6, labelW + 2, 22, juce::Justification::centredLeft);
        g.setColour (t.text);
        g.setFont (uiFont (14.0f, true));
        g.drawText (owner, 18 + labelW + 8, ry + 6, 404 - labelW - 8, 22, juce::Justification::centredLeft, true);
    }

private:
    AtmosBridgeProcessor& proc;
    const UiCtx& ctx;
    std::array<juce::ComboBox, kNumChannels> numBox;
    std::array<std::unique_ptr<NoiseButton>, kNumChannels> noiseBtn;
    std::array<juce::TextButton, kNumChannels> muteBtn;
    juce::TextButton resetBtn, allMuteBtn;
    std::array<float, kNumChannels> rms {};
};

//==============================================================================
// One upmixer slot: name (click = settings window), O/X (on/off), B (bypass), D (delete).
class SlotRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SlotRow (const UiCtx& c) : ctx (c)
    {
        for (auto* b : { &onBtn, &bypBtn, &delBtn })
        {
            b->getProperties().set ("mono", true);
            b->getProperties().set ("radius", 6.0);
            addAndMakeVisible (*b);
        }
        bypBtn.setButtonText ("B");
        delBtn.setButtonText ("D");
        onBtn.setTooltip (tr ("O = 켜짐 / X = 꺼짐 (켜진 업믹서는 하나뿐)"));
        bypBtn.setTooltip (tr ("바이패스: 업믹서를 거치지 않고 호스트 채널을 그대로 사용"));
        delBtn.setTooltip (tr ("이 업믹서를 목록에서 삭제"));
        setTooltip (tr ("이름을 누르면 업믹서 설정 창이 열립니다"));
    }

    std::function<void()> onOpen;

    void setData (const AtmosBridgeProcessor::SlotView& v)
    {
        data = v;
        const auto& t = ctx.th();
        onBtn.setButtonText (v.active ? "O" : "X");
        onBtn.setEnabled (v.loaded);
        styleToggle (onBtn, t, v.active);

        bypBtn.setColour (juce::TextButton::buttonColourId, v.bypass ? t.bypBg : t.btn);
        bypBtn.setColour (juce::TextButton::textColourOffId, v.bypass ? t.bypFg : t.dim);
        setBorder (bypBtn, v.bypass ? t.bypBorder : t.btnBorder);
        bypBtn.setAlpha (v.active ? 1.0f : 0.45f);
        bypBtn.setEnabled (v.loaded);

        stylePlain (delBtn, t);
        delBtn.setColour (juce::TextButton::textColourOffId, t.dim);
        for (auto* b : { &onBtn, &bypBtn, &delBtn }) b->repaint();
        repaint();
    }

    void resized() override
    {
        const int y = (getHeight() - 30) / 2;
        onBtn.setBounds (getWidth() - 120, y, 30, 30);
        bypBtn.setBounds (getWidth() - 82, y, 30, 30);
        delBtn.setBounds (getWidth() - 44, y, 30, 30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = data.active;
        g.setColour (on ? (t.dark ? t.accent.withAlpha (0.08f) : t.panel.interpolatedWith (t.accent, 0.08f)) : t.slotOffBg);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? t.accent : t.slotOffBorder);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        g.setColour (data.loaded ? t.text : t.sub);
        g.setFont (uiFont (15.5f, true));
        g.drawText (data.name, 14, 8, getWidth() - 140, 20, juce::Justification::centredLeft, true);

        juce::String info;
        juce::Colour infoCol = t.sub;
        if (! data.loaded)
        {
            info = data.error.isNotEmpty() ? data.error : tr ("불러오지 못함");
            infoCol = t.clip;
        }
        else if (data.bypass)
        {
            info = tr ("바이패스");
            infoCol = t.bypFg;
        }
        else
        {
            info = data.layout + tr (" \xc2\xb7 지연 ") + juce::String (data.latencyMs, 1) + " ms"
                 + (data.namedLayout ? tr (" \xc2\xb7 채널 순서 자동") : tr (" \xc2\xb7 번호 직접 지정"));
        }
        g.setColour (infoCol);
        g.setFont (uiFont (13.5f));
        g.drawText (info, 14, 29, getWidth() - 140, 18, juce::Justification::centredLeft, true);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.x < getWidth() - 126 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.x < getWidth() - 126 && e.mouseWasClicked() && data.loaded && onOpen) onOpen();
    }

    juce::TextButton onBtn, bypBtn, delBtn;

private:
    const UiCtx& ctx;
    AtmosBridgeProcessor::SlotView data;
};

//==============================================================================
// "+ 업믹서 추가" button with a dashed outline.
class AddButton final : public juce::Button
{
public:
    explicit AddButton (const UiCtx& c) : juce::Button ("add"), ctx (c) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = ctx.th();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (over || down)
        {
            g.setColour ((t.dark ? t.accent : t.neon).withAlpha (down ? 0.12f : 0.06f));
            g.fillRoundedRectangle (r, 8.0f);
        }
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, 8.0f);
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (t.dark ? C (0xffff2bd6).withAlpha (0.5f) : t.spkQuiet);
        g.fillPath (dashed);
        g.setColour (isEnabled() ? t.dim : t.sub.withAlpha (0.6f));
        g.setFont (uiFont (15.0f));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }

private:
    const UiCtx& ctx;
};

//==============================================================================
// Right panel: clipping prevention + upmixer slots.
class RightPanel final : public juce::Component
{
public:
    static constexpr int kSlotY0 = 166, kSlotH = 56, kSlotStep = 66;

    RightPanel (AtmosBridgeProcessor& p, const UiCtx& c) : proc (p), ctx (c), addBtn (c)
    {
        limBtn.getProperties().set ("radius", 16.0);
        limBtn.getProperties().set ("bold", true);
        limBtn.setTooltip (tr ("-1.0 dBFS를 넘지 않게 12채널을 같은 비율로 줄입니다 (지연 0 ms)"));
        limBtn.onClick = [this] { proc.setLimiterOn (! proc.isLimiterOn()); };
        addAndMakeVisible (limBtn);

        bypassBtn.getProperties().set ("radius", 16.0);
        bypassBtn.getProperties().set ("bold", true);
        bypassBtn.setButtonText ("BYPASS");
        bypassBtn.setTooltip (tr ("Atmos Bridge 전체 바이패스: 원본 신호를 호스트 출력으로 그대로 보내고 Atmos 출력은 멈춥니다 "
                                  "(호스트의 바이패스 버튼과 같은 기능)"));
        bypassBtn.onClick = [this] { proc.setBypassed (! proc.isBypassed()); };
        addAndMakeVisible (bypassBtn);

        for (int i = 0; i < kMaxUpmixers; ++i)
        {
            rows[(size_t) i] = std::make_unique<SlotRow> (ctx);
            auto& r = *rows[(size_t) i];
            r.onBtn.onClick  = [this, i] { proc.toggleSlotActive (i); lastError.clear(); };
            r.bypBtn.onClick = [this, i] { proc.toggleSlotBypass (i); lastError.clear(); };
            r.delBtn.onClick = [this, i] { proc.removeSlot (i); lastError.clear(); };
            r.onOpen = [this, i] { if (onOpenSlot) onOpenSlot (i); };
            addChildComponent (r);
        }

        addBtn.onClick = [this] { showAddMenu(); };
        addBtn.setTooltip (tr ("설치된 VST3 업믹서(예: zAuro Pro 2D-3D, zSonic)를 추가합니다"));
        addAndMakeVisible (addBtn);
    }

    std::function<void (int)> onOpenSlot;

    void resized() override
    {
        limBtn.setBounds (18, 72, 82, 32);
        bypassBtn.setBounds (234, 72, 108, 32);
        layoutSlots();
    }

    void refresh()
    {
        const auto& t = ctx.th();
        const bool lim = proc.isLimiterOn();
        if (lim != shownLimiter || themeDirty)
        {
            shownLimiter = lim;
            limBtn.setButtonText (lim ? tr ("켜짐") : tr ("꺼짐"));
            styleToggle (limBtn, t, lim);
            limBtn.repaint();
        }

        const bool byp = proc.isBypassed();
        if (byp != shownBypass || themeDirty)
        {
            shownBypass = byp;
            bypassBtn.setColour (juce::TextButton::buttonColourId, byp ? t.bypBorder : t.btn);
            bypassBtn.setColour (juce::TextButton::textColourOffId, byp ? (t.dark ? C (0xff1a1600) : t.titleA) : t.dim);
            setBorder (bypassBtn, byp ? t.bypBorder : t.btnBorder);
            bypassBtn.repaint();
            repaint();
        }

        const int version = proc.getSlotsVersion();
        const int n = proc.getNumSlots();
        if (version != shownVersion || n != shownCount || themeDirty || ++slowTick % 15 == 0)
        {
            shownVersion = version;
            shownCount = n;
            for (int i = 0; i < kMaxUpmixers; ++i)
            {
                auto& r = *rows[(size_t) i];
                r.setVisible (i < n);
                if (i < n) r.setData (proc.getSlot (i));
            }
            addBtn.setEnabled (n < kMaxUpmixers);
            addBtn.setButtonText (n < kMaxUpmixers ? tr ("+ 업믹서 추가 (최대 4개)") : tr ("업믹서 4개 (최대)"));
            layoutSlots();
            repaint();
        }
        themeDirty = false;
    }

    void themeChanged() { themeDirty = true; addBtn.repaint(); refresh(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        g.setFont (uiFont (13.5f));
        g.setColour (shownBypass ? t.bypFg : t.sub);
        g.drawText (shownBypass ? tr ("바이패스 중") : tr ("꺼짐"), 350, 72, 72, 32, juce::Justification::centredLeft);

        const int n = proc.getNumSlots();
        int y = kSlotY0 + n * kSlotStep + 46;
        if (n == 0)
        {
            g.setColour (t.sub);
            g.setFont (uiFont (14.0f));
            g.drawText (tr ("업믹서 없음: 호스트 채널 1~12를 그대로 사용합니다"), 18, kSlotY0 + 48, 404, 20, juce::Justification::centredLeft, true);
            y += 26;
        }
        if (lastError.isNotEmpty())
        {
            drawWrapped (g, lastError, uiFont (14.0f), t.clip, { 18.0f, (float) y, 404.0f, 44.0f });
        }
    }

private:
    void layoutSlots()
    {
        const int n = proc.getNumSlots();
        for (int i = 0; i < kMaxUpmixers; ++i)
            rows[(size_t) i]->setBounds (18, kSlotY0 + i * kSlotStep, 404, kSlotH);
        addBtn.setBounds (18, kSlotY0 + n * kSlotStep, 404, 40);
    }

    void showAddMenu()
    {
        lastError.clear();
        repaint();
        installed = UpmixerHost::findInstalledVst3();
        juce::PopupMenu m;
        m.addSectionHeader (tr ("설치된 VST3"));
        for (int i = 0; i < installed.size(); ++i)
            m.addItem (i + 1, installed[i].getFileNameWithoutExtension());
        if (installed.isEmpty())
            m.addItem (-1, tr ("(VST3 폴더에서 찾지 못함)"), false);
        m.addSeparator();
        m.addItem (1000, tr ("다른 위치에서 찾기\xe2\x80\xa6"));

        juce::Component::SafePointer<RightPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 1000) { safe->browse(); return; }
            if (r - 1 < safe->installed.size()) safe->addFrom (safe->installed[r - 1]);
        });
    }

    void browse()
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("업믹서 VST3 선택"),
                                                       juce::File ("C:\\Program Files\\Common Files\\VST3"), "*.vst3");
        juce::Component::SafePointer<RightPanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f != juce::File()) safe->addFrom (f);
                              });
    }

    void addFrom (const juce::File& f)
    {
        juce::MouseCursor::showWaitCursor();
        juce::String err;
        const bool ok = proc.addUpmixer (f.getFullPathName(), err);
        juce::MouseCursor::hideWaitCursor();
        lastError = ok ? juce::String() : err;
        refresh();
        repaint();
        if (ok && onOpenSlot) onOpenSlot (proc.getNumSlots() - 1);
    }

    AtmosBridgeProcessor& proc;
    const UiCtx& ctx;
    juce::TextButton limBtn, bypassBtn;
    std::array<std::unique_ptr<SlotRow>, kMaxUpmixers> rows;
    AddButton addBtn;
    juce::Array<juce::File> installed;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastError;
    bool shownLimiter = false, shownBypass = false, themeDirty = true;
    int shownVersion = -1, shownCount = -1, slowTick = 0;
};

//==============================================================================
// Everything that does not change while audio plays: page, panels, title, headings. Cached as an image.
class BackgroundView final : public juce::Component
{
public:
    explicit BackgroundView (const UiCtx& c) : ctx (c)
    {
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        const auto b = getLocalBounds().toFloat();

        // Page
        juce::ColourGradient page (t.bgTop, 0, 0, t.bgBottom, 0, b.getHeight(), false);
        page.addColour (0.45, t.bgMid);
        g.setGradientFill (page);
        g.fillAll();
        if (t.grid)
        {
            g.setColour (C (0xff00f0ff).withAlpha (0.05f));
            for (float x = 0; x < b.getWidth(); x += 40.0f) g.fillRect (x, 0.0f, 1.0f, b.getHeight());
            for (float y = 0; y < b.getHeight(); y += 40.0f) g.fillRect (0.0f, y, b.getWidth(), 1.0f);
        }

        // Panels
        drawPanel (g, kHeader, t, t.headerEnd, t.headerBorder);
        drawPanel (g, kFooter, t, t.footerEnd, t.footerBorder);
        if (t.shadow)
            juce::DropShadow (t.shadowColour, 22, { 0, 6 }).drawForPath (g, panelShape (kRoom.toFloat(), t));
        drawPanel (g, kMap.toFloat(), t);
        drawPanel (g, kRight.toFloat(), t);

        paintTitle (g, t);

        // Mode group
        {
            const auto shape = t.chamfer ? panelShape (kModeGroup, t, 10.0f)
                                         : [&] { juce::Path p; p.addRoundedRectangle (kModeGroup, 10.0f); return p; }();
            g.setColour (t.dark ? t.field : t.field);
            g.fillPath (shape);
            g.setColour (t.dark ? C (0xffff2bd6).withAlpha (0.4f) : t.panelBorder);
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        // Map panel static texts
        g.saveState();
        g.setOrigin (kMap.getPosition());
        drawHeading (g, t, tr ("채널 매핑"), { 18, 14, 200, 26 });
        g.setColour (t.sub);
        g.setFont (uiFont (14.5f));
        g.drawText (tr ("입력 채널마다 출력 위치 번호를 지정합니다."), 18, 40, 230, 18, juce::Justification::centredLeft, true);
        g.drawText (tr ("0=끄기"), 18, 58, 230, 18, juce::Justification::centredLeft, true);
        g.setFont (uiFont (14.0f));
        g.drawText (tr ("입력"), 18, 106, 50, 16, juce::Justification::centredLeft);
        g.drawText (tr ("번호"), 76, 106, 56, 16, juce::Justification::centredLeft);
        g.drawText (tr ("출력 위치"), 140, 106, 80, 16, juce::Justification::centredLeft);
        g.drawText (tr ("노이즈"), 226, 106, 38, 16, juce::Justification::centred);
        g.drawText (tr ("뮤트"), 268, 106, 38, 16, juce::Justification::centred);
        g.drawText (tr ("레벨"), 312, 106, 60, 16, juce::Justification::centredLeft);
        drawWrapped (g, tr ("핑크 노이즈 -20 dBFS RMS (LFE는 120 Hz 이하) \xc2\xb7 테스트 중에는 음악이 잠시 멈춥니다"),
                     uiFont (14.0f), t.sub, { 18, 570, 404, 44 });
        g.restoreState();

        // Right panel static texts
        g.saveState();
        g.setOrigin (kRight.getPosition());
        drawHeading (g, t, tr ("클리핑 방지"), { 18, 14, 190, 26 });
        g.setColour (t.sub);
        g.setFont (uiFont (14.5f));
        g.drawText (tr ("-1.0 dBFS \xc2\xb7 12채널 연동"), 18, 42, 196, 20, juce::Justification::centredLeft);
        g.setColour (t.divider);
        g.fillRect (220.0f, 18.0f, 1.0f, 86.0f);
        drawHeading (g, t, "BYPASS", { 234, 14, 188, 26 });
        g.setColour (t.sub);
        g.setFont (uiFont (14.5f));
        g.drawText (tr ("Atmos Bridge 7.1.4ch 전체"), 234, 42, 188, 20, juce::Justification::centredLeft);
        g.setColour (t.divider);
        g.fillRect (18.0f, 118.0f, 404.0f, 1.0f);
        drawHeading (g, t, tr ("업믹서"), { 18, 130, 100, 26 });
        g.setColour (t.sub);
        g.setFont (uiFont (14.0f));
        g.drawText (tr ("O/X 켜기\xc2\xb7끄기 (하나만 켜짐) \xc2\xb7 B 바이패스 \xc2\xb7 D 삭제"), 104, 130, 318, 26,
                    juce::Justification::centredRight, true);
        drawWrapped (g, tr ("이름을 누르면 설정 창 \xc2\xb7 업믹서 입력: 호스트 채널 1~2 \xc2\xb7 "
                            "슬롯마다 채널 번호표를 따로 기억 \xc2\xb7 설정은 프로젝트와 함께 저장"),
                     uiFont (14.0f), t.sub, { 18, 548, 404, 60 });
        g.restoreState();
    }

private:
    void paintTitle (juce::Graphics& g, const Theme& t)
    {
        float textX = 78.0f;
        if (t.roomStyle == 2)
        {
            // Sunset over the sea
            const juce::Rectangle<float> logo (38.0f, 31.0f, 38.0f, 38.0f);
            juce::Path circle; circle.addEllipse (logo);
            juce::DropShadow (t.neon.withAlpha (0.30f), 12, { 0, 4 }).drawForPath (g, circle);
            juce::ColourGradient grad (C (0xffffc47a), 0, logo.getY(), C (0xff0656b5), 0, logo.getBottom(), false);
            grad.addColour (0.46, C (0xffff8f73));
            grad.addColour (0.54, t.neon);
            g.setGradientFill (grad);
            g.fillPath (circle);
            drawSpeakerIcon (g, logo.reduced (8.0f), juce::Colours::white, 2.0f);
            textX = 90.0f;
        }
        else
        {
            drawSpeakerIcon (g, { 40.0f, 37.0f, 26.0f, 26.0f }, t.neon.withMultipliedBrightness (t.dark ? 1.0f : 0.95f), 1.8f);
        }

        const bool cyber = t.id == ThemeId::Cyberpunk;
        const auto f = titleFont (t, cyber ? 21.0f : 22.0f);
        const juce::String a = cyber ? "ATMOS BRIDGE " : "Atmos Bridge ";
        const juce::String bText = "7.1.4ch";
        const float wa = juce::GlyphArrangement::getStringWidth (f, a);
        const float wb = juce::GlyphArrangement::getStringWidth (f, bText);
        const juce::Rectangle<float> ra (textX, 26.0f, wa + 4.0f, 28.0f), rb (textX + wa, 26.0f, wb + 6.0f, 28.0f);

        if (t.titleHighlight.getAlpha() > 0)
        {
            g.setColour (t.titleHighlight);
            g.fillRect (rb.getX() - 1.0f, rb.getY() + 16.0f, wb + 4.0f, 10.0f);
        }
        if (cyber)
        {
            // Chromatic offset + neon glow
            drawTextPath (g, a, f, ra.translated (2.0f, 0.0f), juce::Justification::centredLeft, C (0xffff2bd6).withAlpha (0.85f));
            drawTextPath (g, a, f, ra.translated (-1.0f, 0.0f), juce::Justification::centredLeft, C (0xfffcee0a).withAlpha (0.5f));
            drawTextPath (g, a, f, ra, juce::Justification::centredLeft, t.titleA, t.titleA.withAlpha (0.6f), 12);
            drawTextPath (g, bText, f, rb, juce::Justification::centredLeft, t.titleB, t.titleB.withAlpha (0.8f), 10);
        }
        else
        {
            drawTextPath (g, a, f, ra, juce::Justification::centredLeft, t.titleA);
            drawTextPath (g, bText, f, rb, juce::Justification::centredLeft, t.titleB);
        }

        if (cyber)
        {
            g.setColour (t.sub);
            g.setFont (monoFont (14.0f).withExtraKerningFactor (0.12f));
            g.drawText (tr ("// WINDOWS SPATIAL AUDIO \xe2\x86\x92 ATMOS (MAT)"), (int) textX, 53, 440, 16, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (t.sub);
            g.setFont (uiFont (14.0f));
            g.drawText (tr ("Windows Spatial Audio \xe2\x86\x92 Atmos (MAT)"), (int) textX, 53, 400, 16, juce::Justification::centredLeft);
        }
    }

    const UiCtx& ctx;
};

//==============================================================================
// Thin scanlines over everything (Cyberpunk only).
class ScanlineOverlay final : public juce::Component
{
public:
    explicit ScanlineOverlay (const UiCtx& c) : ctx (c) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        if (! ctx.th().scanlines) return;
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        const auto clip = g.getClipBounds();
        const int y0 = juce::jmax (0, clip.getY() - clip.getY() % 3);
        for (int y = y0; y < clip.getBottom(); y += 3)
            g.fillRect (clip.getX(), y, clip.getWidth(), 1);
    }
private:
    const UiCtx& ctx;
};

//==============================================================================
class DeviceBox final : public juce::ComboBox
{
public:
    std::function<void()> beforePopup;
    void showPopup() override
    {
        if (beforePopup) beforePopup();
        juce::ComboBox::showPopup();
    }
};

//==============================================================================
class RootView final : public juce::Component
{
public:
    RootView (AtmosBridgeProcessor& p, UiCtx& c)
        : proc (p), ctx (c), background (c), room (p, c), map (p, c), right (p, c), scanlines (c)
    {
        addAndMakeVisible (background);
        addAndMakeVisible (room);
        addAndMakeVisible (map);
        addAndMakeVisible (right);

        right.onOpenSlot = [this] (int i) { openSlotWindow (i); };
        for (int h = 0; h < kMaxUpmixers; ++h)
            proc.getHost (h).beforeUnload = [this, h] { windows[(size_t) h].reset(); };

        static const char* modeNames[kNumModes] = { "기본", "저지연", "초저지연" };
        static const char* modeTips[kNumModes] = {
            "기본 모드: 여유 버퍼 100 ms (가장 안정적)",
            "저지연 모드: 여유 버퍼 8 ms, 끊기면 10 ms씩 자동으로 늘림 (최대 +60 ms)",
            "초저지연 모드: 여유 버퍼 4.5 ms (최소), 자동 보정 없음 \xc2\xb7 끊김은 횟수로만 표시"
        };
        for (int m = 0; m < kNumModes; ++m)
        {
            auto& b = modeBtn[(size_t) m];
            b.setButtonText (tr (modeNames[m]));
            b.setTooltip (tr (modeTips[m]));
            b.getProperties().set ("radius", 7.0);
            b.onClick = [this, m] { proc.setMode ((LatencyMode) m); };
            addAndMakeVisible (b);
        }

        for (int i = 0; i < kNumThemes; ++i)
            themeBox.addItem (themeFor ((ThemeId) i).menuName, i + 1);
        themeBox.setTooltip (tr ("화면 테마. 고른 테마는 이 PC에서 다음에 열 때도 그대로 유지됩니다."));
        themeBox.onChange = [this]
        {
            const int id = themeBox.getSelectedId();
            if (id > 0 && (ThemeId) (id - 1) != proc.getTheme()) proc.setTheme ((ThemeId) (id - 1));
        };
        addAndMakeVisible (themeBox);

        device.setTooltip (tr ("Atmos 출력 장치. '자동'은 공간 음향이 켜진 장치(AVR)를 자동으로 고릅니다."));
        device.beforePopup = [this] { fillDevices(); };
        device.onChange = [this]
        {
            const int idx = device.getSelectedItemIndex();
            if (idx < 0 || idx >= deviceIds.size()) return;
            proc.setOutputDeviceId (deviceIds[idx]);
        };
        fillDevices();
        addAndMakeVisible (device);

        seqBtn.onClick = [this] { proc.toggleSequence(); };
        seqBtn.setTooltip (tr ("12개 위치에 차례로 핑크 노이즈를 2초씩 내보냅니다"));
        addAndMakeVisible (seqBtn);
        stopBtn.setButtonText (tr ("테스트 정지"));
        stopBtn.onClick = [this] { proc.stopTests(); };
        addAndMakeVisible (stopBtn);

        statusTip.setInterceptsMouseClicks (true, false);
        addAndMakeVisible (statusTip);
        addAndMakeVisible (scanlines);

        setSize (kBaseWidth, kBaseHeight);
    }

    ~RootView() override
    {
        for (int h = 0; h < kMaxUpmixers; ++h) proc.getHost (h).beforeUnload = nullptr;
        for (auto& w : windows) w.reset();
    }

    void openSlotWindow (int slot)
    {
        const int h = proc.getSlotHostIndex (slot);
        if (h < 0) return;
        auto& w = windows[(size_t) h];
        if (w != nullptr) { w->toFront (true); return; }
        if (auto* inst = proc.getSlotInstance (slot))
        {
            juce::Component::SafePointer<RootView> safe (this);
            w = std::make_unique<HostedPluginWindow> (*inst, ctx.th().bgTop, [safe, h]
            {
                // Deleting the window from inside its own callback: defer to the next message.
                juce::MessageManager::callAsync ([safe, h] { if (safe != nullptr) safe->windows[(size_t) h].reset(); });
            });
        }
    }

    void themeChanged()
    {
        const auto& t = ctx.th();
        themeBox.setSelectedId ((int) t.id + 1, juce::dontSendNotification);
        themeBox.setColour (juce::ComboBox::backgroundColourId, t.themeBoxBg);
        themeBox.setColour (juce::ComboBox::textColourId, t.themeBoxText);
        themeBox.setColour (juce::ComboBox::outlineColourId, t.themeBoxBorder);
        themeBox.setColour (juce::ComboBox::arrowColourId, t.themeBoxText);
        device.setColour (juce::ComboBox::backgroundColourId, t.field);
        device.setColour (juce::ComboBox::textColourId, t.text);
        device.setColour (juce::ComboBox::outlineColourId, t.btnBorder);
        device.setColour (juce::ComboBox::arrowColourId, t.sub);
        stylePlain (stopBtn, t);

        background.repaint();
        room.themeChanged();
        map.themeChanged();
        right.themeChanged();
        lastMode = -1;
        refresh();
        if (livePainter != nullptr) livePainter->repaint();
    }

    void resized() override
    {
        background.setBounds (getLocalBounds());
        scanlines.setBounds (getLocalBounds());
        themeBox.setBounds (538, 34, 170, 32);
        device.setBounds (718, 34, 260, 32);
        for (int m = 0; m < kNumModes; ++m)
            modeBtn[(size_t) m].setBounds (1486 + m * 70, 35, 66, 30);
        room.setBounds (kRoom);
        map.setBounds (kMap);
        right.setBounds (kRight);
        seqBtn.setBounds (40, 744, 136, 36);
        stopBtn.setBounds (184, 744, 104, 36);
        statusTip.setBounds (kStatusPill.toNearestInt());
    }

    void refresh()
    {
        const auto& t = ctx.th();
        room.refresh();
        map.refresh();
        right.refresh();

        const int mode = (int) proc.getMode();
        if (mode != lastMode)
        {
            lastMode = mode;
            for (int m = 0; m < kNumModes; ++m)
            {
                auto& b = modeBtn[(size_t) m];
                const bool on = m == mode;
                b.setColour (juce::TextButton::buttonColourId,
                             on ? (t.solidActive ? t.accent : t.accent.withAlpha (0.16f)) : juce::Colours::transparentBlack);
                b.setColour (juce::TextButton::textColourOffId, on ? t.activeText : t.dim);
                setBorder (b, juce::Colours::transparentBlack);
                b.repaint();
            }
        }

        const bool seq = proc.isSequenceOn();
        seqBtn.setButtonText (seq ? tr ("순차 테스트 중지") : tr ("순차 테스트 톤"));
        seqBtn.setColour (juce::TextButton::buttonColourId, t.btn);
        seqBtn.setColour (juce::TextButton::textColourOffId, t.text);
        setBorder (seqBtn, seq ? t.neon : t.btnBorder);
        seqBtn.repaint();

        if (! device.isPopupActive())
        {
            const auto want = proc.getOutputDeviceId();
            const int idx = deviceIds.indexOf (want);
            if (idx >= 0 && device.getSelectedItemIndex() != idx)
                device.setSelectedItemIndex (idx, juce::dontSendNotification);
        }

        updateStatus();
        repaintLive (kStatusPill.expanded (2).toNearestInt());
        repaintLive (kRatePill.expanded (2).toNearestInt());
        repaintLive (kFooter.toNearestInt());
    }

    // Live header/footer texts (drawn by an overlay so the cached background stays untouched).
    void paintLive (juce::Graphics& g)
    {
        const auto& t = ctx.th();

        // Status pill
        g.setColour (statusColour.withAlpha (t.dark ? 0.10f : 0.12f)); g.fillRoundedRectangle (kStatusPill, 16);
        g.setColour (statusColour.withAlpha (0.45f)); g.drawRoundedRectangle (kStatusPill.reduced (0.5f), 16, 1);
        if (t.dark) { g.setColour (statusColour.withAlpha (0.5f)); g.fillEllipse (kStatusPill.getX() + 11, kStatusPill.getCentreY() - 6, 12, 12); }
        g.setColour (statusColour); g.fillEllipse (kStatusPill.getX() + 13, kStatusPill.getCentreY() - 4, 8, 8);
        g.setColour (t.text); g.setFont (uiFont (15.5f));
        g.drawText (statusText, kStatusPill.withTrimmedLeft (28).withTrimmedRight (10).toNearestInt(), juce::Justification::centredLeft, true);

        // Rate pill
        const bool rateOk = std::abs (proc.getHostSampleRate() - 48000.0) < 1.0;
        g.setColour (t.pillBg); g.fillRoundedRectangle (kRatePill, 16);
        g.setColour (rateOk ? t.pillBorder : t.amber); g.drawRoundedRectangle (kRatePill.reduced (0.5f), 16, 1);
        g.setColour (rateOk ? t.text : t.amber); g.setFont (monoFont (14.0f));
        g.drawText (rateText, kRatePill.toNearestInt(), juce::Justification::centred);

        // Footer
        g.setColour (t.sub); g.setFont (uiFont (15.0f));
        g.drawText (testText, 298, 744, 440, 36, juce::Justification::centredLeft, true);

        g.setFont (uiFont (15.5f));
        g.drawText (modeText, 700, 744, 656, 36, juce::Justification::centredRight, true);

        g.setColour (dropouts > 0 ? t.amber : t.sub);
        g.drawText (tr ("끊김 ") + juce::String (dropouts), kDropouts.toNearestInt(), juce::Justification::centred);

        g.setColour (clockColour); g.fillEllipse (1460, 758, 8, 8);
        g.setColour (t.text); g.setFont (uiFont (15.0f));
        g.drawText (clockText, 1474, 744, 232, 36, juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (kDropouts.contains (e.position))
            if (auto* s = proc.getSharedOutput()) s->feeder.resetDropouts();
    }

private:
    void fillDevices()
    {
        deviceIds.clear();
        device.clear (juce::dontSendNotification);
        deviceIds.add ({});
        device.addItem (tr ("자동 (공간 음향 켜진 AVR/HDMI, 가상 케이블 제외)"), 1);
        const auto list = SpatialEngine::listOutputDevices();
        int id = 2;
        for (const auto& d : list)
        {
            deviceIds.add (juce::String::fromUTF8 (d.id.c_str()));
            device.addItem (juce::String::fromUTF8 (d.name.c_str())
                                + (SpatialEngine::isVirtualDeviceName (d.name) ? tr (" (가상 케이블: 피드백 주의)") : juce::String()),
                            id++);
        }
        const auto want = proc.getOutputDeviceId();
        if (want.isNotEmpty() && ! deviceIds.contains (want))
        {
            deviceIds.add (want);
            device.addItem (tr ("저장된 장치 (연결 안 됨)"), id++);
        }
        device.setSelectedItemIndex (juce::jmax (0, deviceIds.indexOf (want)), juce::dontSendNotification);
    }

    void updateStatus()
    {
        const auto& t = ctx.th();
        juce::String tip;
        if (! proc.isOutputOwner())
        {
            statusColour = t.sub;
            statusText = tr ("다른 인스턴스가 출력 중 (이 창은 비활성)");
        }
        else if (auto* s = proc.getSharedOutput())
        {
            const auto st = s->engine.getStatus();
            const auto name = juce::String::fromUTF8 (s->engine.getDeviceName().c_str());
            tip = name + "\n" + juce::String::fromUTF8 (s->engine.getLastError().c_str());
            switch (st)
            {
                case EngineStatus::Running:
                    if (SpatialEngine::isVirtualDeviceName (s->engine.getDeviceName()))
                    {
                        // The host captures from the virtual cable: output there loops back (feedback, no sound at the AVR).
                        statusColour = t.amber;
                        statusText = tr ("가상 케이블로 출력 중: 장치를 AVR로 선택하세요");
                    }
                    else
                    {
                        statusColour = t.ok;
                        statusText = tr ("출력 중 \xc2\xb7 ") + name;
                    }
                    break;
                case EngineStatus::Connecting:
                    statusColour = t.amber;
                    statusText = tr ("연결 중\xe2\x80\xa6");
                    break;
                case EngineStatus::SpatialOff:
                    statusColour = t.amber;
                    statusText = tr ("공간 음향 꺼짐: Dolby Atmos for home theater 선택");
                    break;
                case EngineStatus::NoDevice:
                    statusColour = t.red;
                    statusText = tr ("출력 장치 없음");
                    break;
                case EngineStatus::Error:
                    statusColour = t.red;
                    statusText = tr ("오류 (마우스를 올려 자세히 보기)");
                    break;
                case EngineStatus::Unsupported:
                    statusColour = t.sub;
                    statusText = tr ("Windows 전용");
                    break;
                case EngineStatus::Stopped:
                default:
                    statusColour = t.sub;
                    statusText = tr ("정지");
                    break;
            }

            const double outRate = s->engine.getOutputRate();
            rateText = juce::String (outRate / 1000.0, outRate == std::floor (outRate / 1000.0) * 1000.0 ? 0 : 1) + tr (" kHz \xc2\xb7 12ch");

            const bool running = st == EngineStatus::Running;
            static const char* modeNames[kNumModes] = { "기본 모드", "저지연 모드", "초저지연 모드" };
            const int mode = (int) proc.getMode();
            const double margin = s->feeder.getMarginMs();
            const double upLat = proc.getUpmixerLatencyMs();
            modeText = tr (modeNames[mode]) + tr (" \xc2\xb7 여유 버퍼 ")
                     + juce::String (margin, margin == std::floor (margin) ? 0 : 1)
                     + tr (" ms \xc2\xb7 플러그인 지연 ")
                     + (running && s->feeder.getState() == OutputFeeder::State::Running
                            ? juce::String (s->feeder.getLatencyMs() + upLat, 1) + " ms" : juce::String ("-- ms"))
                     + (upLat > 0.0 ? tr (" (업믹서 포함)") : juce::String());
            dropouts = (int) s->feeder.getDropouts();

            const float drift = std::abs (s->feeder.getDriftPpm());
            if (! running)                          { clockColour = t.sub;   clockText = tr ("출력 대기"); }
            else if (! s->feeder.isProducerAlive()) { clockColour = t.sub;   clockText = tr ("입력 신호 없음 (호스트 정지?)"); }
            else if (s->feeder.isProducerBursty())  { clockColour = t.amber; clockText = tr ("호스트 처리 불규칙"); }
            else if (drift > 300.0f)                { clockColour = t.amber; clockText = tr ("클럭 차이 큼: 호스트 출력=AVR 권장"); }
            else                                    { clockColour = t.ok;    clockText = tr ("클럭: 호스트와 동기 정상"); }
        }

        if (proc.isBypassed())
        {
            statusColour = t.bypBorder;
            statusText = tr ("BYPASS \xc2\xb7 원본 신호가 호스트 출력으로 그대로 나갑니다");
        }

        const int iNoise = proc.getInputNoise();
        const int seqPos = proc.getSequencePosition();
        const int pNoise = seqPos >= 0 ? seqPos : proc.getPositionNoise();
        if (pNoise >= 0)
            testText = tr ("핑크 노이즈 -20 dBFS \xe2\x86\x92 ") + kPosCodes[(size_t) pNoise];
        else if (iNoise >= 0)
        {
            const int pos = proc.getPosition (iNoise);
            testText = tr ("핑크 노이즈 -20 dBFS \xe2\x86\x92 Ch ") + juce::String (iNoise + 1) + " ("
                     + (pos >= 0 ? juce::String (kPosCodes[(size_t) pos]) : tr ("끄기")) + ")"
                     + (proc.isMuted (iNoise) ? tr (" \xc2\xb7 뮤트됨") : juce::String());
        }
        else
            testText = {};

        if (tip != lastTip) { lastTip = tip; statusTip.setTooltip (tip); }
    }

    // Invisible component over the status pill: carries its tooltip and draws the live header/footer texts.
    struct LiveLayer final : public juce::Component, public juce::SettableTooltipClient {};

    // Draws the live texts between the cached background and the controls.
    struct LivePainter final : public juce::Component
    {
        explicit LivePainter (RootView& r) : root (r) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { root.paintLive (g); }
        RootView& root;
    };

public:
    void addLivePainter()
    {
        livePainter = std::make_unique<LivePainter> (*this);
        addAndMakeVisible (*livePainter);
        livePainter->toBehind (&room);
        livePainter->setBounds (getLocalBounds());
    }

    void repaintLive (juce::Rectangle<int> area)
    {
        if (livePainter != nullptr) livePainter->repaint (area);
    }

private:
    AtmosBridgeProcessor& proc;
    UiCtx& ctx;
    BackgroundView background;
    RoomView room;
    MapPanel map;
    RightPanel right;
    ScanlineOverlay scanlines;
    std::unique_ptr<LivePainter> livePainter;
    juce::ComboBox themeBox;
    DeviceBox device;
    juce::StringArray deviceIds;
    std::array<juce::TextButton, kNumModes> modeBtn;
    juce::TextButton seqBtn, stopBtn;
    LiveLayer statusTip;
    juce::String lastTip;
    int lastMode = -1;

    std::array<std::unique_ptr<HostedPluginWindow>, kMaxUpmixers> windows;

    juce::Colour statusColour { juce::Colours::grey }, clockColour { juce::Colours::grey };
    juce::String statusText, rateText { tr ("48 kHz \xc2\xb7 12ch") }, modeText, testText, clockText;
    int dropouts = 0;
};

//==============================================================================
AtmosBridgeEditor::AtmosBridgeEditor (AtmosBridgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    ctx = std::make_unique<UiCtx>();
    lnf = std::make_unique<NeonLookAndFeel>();
    setLookAndFeel (lnf.get());

    root = std::make_unique<RootView> (proc, *ctx);
    root->addLivePainter();
    addAndMakeVisible (*root);

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
        c->setSizeLimits (kBaseWidth / 2, kBaseHeight / 2, (int) (kBaseWidth * 1.5), (int) (kBaseHeight * 1.5));
    }
    shownTheme = proc.getTheme();
    applyTheme();
    const float s = proc.getUiScale();
    setSize (juce::roundToInt (kBaseWidth * s), juce::roundToInt (kBaseHeight * s));
    startTimerHz (30);
}

AtmosBridgeEditor::~AtmosBridgeEditor()
{
    stopTimer();
    root.reset();
    setLookAndFeel (nullptr);
}

void AtmosBridgeEditor::applyTheme()
{
    ctx->t = &themeFor (shownTheme);
    lnf->setTheme (*ctx->t);
    sendLookAndFeelChange();
    if (root != nullptr) root->themeChanged();
    repaint();
}

void AtmosBridgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (themeFor (shownTheme).bgTop);
}

void AtmosBridgeEditor::resized()
{
    if (root == nullptr) return;
    const float s = (float) getWidth() / (float) kBaseWidth;
    root->setTransform (juce::AffineTransform::scale (s));
    root->setBounds (0, 0, kBaseWidth, kBaseHeight);
    proc.setUiScale (s);
}

void AtmosBridgeEditor::timerCallback()
{
    if (proc.getTheme() != shownTheme)
    {
        shownTheme = proc.getTheme();
        applyTheme();
    }
    if (root != nullptr) root->refresh();
}
