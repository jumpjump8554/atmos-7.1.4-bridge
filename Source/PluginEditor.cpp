#include "PluginEditor.h"

using namespace ab;

//==============================================================================
namespace
{
juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

namespace col
{
    const juce::Colour bg        { 0xff0f1214 };
    const juce::Colour panel     { 0xff161a1d };
    const juce::Colour border    { 0xff232a2e };
    const juce::Colour roomBg    { 0xff0b0d0f };
    const juce::Colour text      { 0xffe6eae6 };
    const juce::Colour sub       { 0xff9aa3a0 };
    const juce::Colour dim       { 0xffc7cec9 };
    const juce::Colour neon      { 0xff39ff14 };
    const juce::Colour amber     { 0xffffc23d };
    const juce::Colour red       { 0xffff6b6b };
    const juce::Colour btn       { 0xff1d2327 };
    const juce::Colour btnBorder { 0xff2f383d };
    const juce::Colour muteBg    { 0xff5a1f1f };
    const juce::Colour muteBdr   { 0xffb04040 };
    const juce::Colour muteFg    { 0xffffc9c9 };
    const juce::Colour field     { 0xff0b0d0f };
}

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
        setColour (juce::ComboBox::backgroundColourId, col::field);
        setColour (juce::ComboBox::textColourId, col::text);
        setColour (juce::ComboBox::outlineColourId, col::btnBorder);
        setColour (juce::ComboBox::arrowColourId, col::sub);
        setColour (juce::PopupMenu::backgroundColourId, col::panel);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::neon.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, col::text);
        setColour (juce::TooltipWindow::backgroundColourId, col::panel);
        setColour (juce::TooltipWindow::textColourId, col::text);
        setColour (juce::TooltipWindow::outlineColourId, col::border);
        setColour (juce::TextButton::buttonColourId, col::btn);
        setColour (juce::TextButton::textColourOffId, col::text);
        setColour (juce::TextButton::textColourOnId, col::text);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = (float) (double) b.getProperties().getWithDefault ("radius", 7.0);
        auto fill = backgroundColour;
        if (down) fill = fill.brighter (0.15f);
        else if (over) fill = fill.brighter (0.07f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        const auto bdr = b.getProperties().getWithDefault ("border", (juce::int64) col::btnBorder.getARGB());
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
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
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
};

//==============================================================================
// Pink-noise button with a small waveform icon.
class NoiseButton final : public juce::Button
{
public:
    NoiseButton() : juce::Button ("noise") {}
    bool active = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        auto fill = active ? col::neon.withAlpha (0.18f) : col::btn;
        if (down) fill = fill.brighter (0.15f); else if (over) fill = fill.brighter (0.07f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (active ? col::neon : col::btnBorder);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        // waveform: M1 7h2l1.5-5 2 10 2-8 1.5 6 1.5-4 1.5 3 1-2H17 in an 18x14 box
        const float ox = r.getCentreX() - 9.0f, oy = r.getCentreY() - 7.0f;
        juce::Path p;
        const float pts[][2] = { {1,7},{3,7},{4.5f,2},{6.5f,12},{8.5f,4},{10,10},{11.5f,6},{13,9},{14,7},{17,7} };
        p.startNewSubPath (ox + pts[0][0], oy + pts[0][1]);
        for (int i = 1; i < 10; ++i) p.lineTo (ox + pts[i][0], oy + pts[i][1]);
        g.setColour (active ? col::neon : col::dim);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
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
class RoomView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RoomView (AtmosBridgeProcessor& p) : proc (p)
    {
        setTooltip (tr ("스피커를 클릭하면 해당 위치로 핑크 노이즈(-20 dBFS)가 나갑니다. 다시 클릭하면 멈춥니다."));
    }

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

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        juce::Path clip;
        clip.addRoundedRectangle (bounds, 10.0f);
        g.saveState();
        g.reduceClipRegion (clip);

        g.fillAll (col::roomBg);
        auto poly = [] (std::initializer_list<juce::Point<float>> pts)
        {
            juce::Path p;
            bool first = true;
            for (auto pt : pts) { if (first) p.startNewSubPath (pt); else p.lineTo (pt); first = false; }
            p.closeSubPath();
            return p;
        };
        g.setColour (juce::Colour (0xff14181b)); g.fillPath (poly ({ {0,0}, {780,0}, {540,170}, {240,170} }));      // ceiling
        g.setColour (juce::Colour (0xff101316)); g.fillPath (poly ({ {0,0}, {240,170}, {240,360}, {0,620} }));      // left wall
        g.setColour (juce::Colour (0xff101316)); g.fillPath (poly ({ {780,0}, {540,170}, {540,360}, {780,620} }));  // right wall
        g.setColour (juce::Colour (0xff12161a)); g.fillRect (240.0f, 170.0f, 300.0f, 190.0f);                    // back wall
        g.setColour (juce::Colour (0xff0d1012)); g.fillPath (poly ({ {240,360}, {540,360}, {780,620}, {0,620} })); // floor

        // Title
        g.setColour (col::text);
        g.setFont (uiFont (17.0f, true));
        g.drawText (tr ("7.1.4 스피커 배치"), 0, 10, 780, 22, juce::Justification::centred);
        g.setColour (col::sub);
        g.setFont (uiFont (15.5f));
        g.drawText (tr ("스피커를 클릭하면 해당 위치로 핑크 노이즈(-20 dBFS) 출력"), 0, 32, 780, 18, juce::Justification::centred);

        // Screen and listening position
        g.setColour (juce::Colour (0xff1b2024)); g.fillRoundedRectangle (270, 190, 240, 100, 3);
        g.setColour (juce::Colour (0xff2a3136)); g.drawRoundedRectangle (270, 190, 240, 100, 3, 1);
        g.setColour (juce::Colour (0xff8c9692)); g.setFont (monoFont (14.0f));
        g.drawText ("S C R E E N", 270, 190, 240, 100, juce::Justification::centred);
        g.setColour (juce::Colour (0xff20262a)); g.fillRoundedRectangle (280, 448, 220, 54, 10);
        g.setColour (juce::Colour (0xff2c3439)); g.drawRoundedRectangle (280, 448, 220, 54, 10, 1);
        g.setColour (col::sub); g.setFont (uiFont (14.5f));
        g.drawText (tr ("청취 위치"), 280, 448, 220, 54, juce::Justification::centred);

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
            const float l = level[(size_t) p];
            if (l <= 0.01f) continue;
            juce::Path beam;
            beam.startNewSubPath (s.bx + s.bw * 0.42f, s.by);
            beam.lineTo (s.bx + s.bw * 0.58f, s.by);
            beam.lineTo (s.bx + s.bw, s.by + s.bh);
            beam.lineTo (s.bx, s.by + s.bh);
            beam.closeSubPath();
            juce::ColourGradient grad (col::neon.withAlpha (0.5f * l), s.bx, s.by, col::neon.withAlpha (0.0f), s.bx, s.by + s.bh, false);
            g.setGradientFill (grad);
            g.fillPath (beam);
        }

        const int seq = proc.getSequencePosition();
        const int posNoise = seq >= 0 ? seq : proc.getPositionNoise();
        for (int p = 0; p < kNumChannels; ++p)
            drawSpeaker (g, p, level[(size_t) p], clipHold[(size_t) p] > 0, p == posNoise,
                         numbers[p].isEmpty() ? tr ("\xe2\x80\x93") : numbers[p]);

        g.restoreState();
        g.setColour (col::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (hitSpeaker (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int p = hitSpeaker (e.position);
        if (p >= 0) proc.togglePositionNoise (p);
    }

private:
    int hitSpeaker (juce::Point<float> pt) const
    {
        for (int p = 0; p < kNumChannels; ++p)
        {
            const auto& s = kGeo[p];
            if (juce::Rectangle<float> (s.x, s.y, s.w, s.h + 18.0f).expanded (8.0f).contains (pt)) return p;
        }
        return -1;
    }

    void drawSpeaker (juce::Graphics& g, int p, float l, bool clip, bool testing, const juce::String& number)
    {
        const auto& s = kGeo[p];
        const juce::Rectangle<float> r (s.x, s.y, s.w, s.h);
        juce::Path shape;
        if (s.ellipse) shape.addEllipse (r);
        else           shape.addRoundedRectangle (r, s.radius);

        const auto glow = clip ? col::amber : col::neon;
        if (l > 0.02f)
        {
            juce::DropShadow (glow.withAlpha (juce::jlimit (0.0f, 1.0f, 0.15f + 0.8f * l)),
                              (int) (4.0f + 26.0f * l), {}).drawForPath (g, shape);
        }
        g.setColour (juce::Colour (0xff0c0f0d));
        g.fillPath (shape);
        if (l > 0.02f)
        {
            g.setColour (glow.withAlpha (0.05f + 0.22f * l));
            g.fillPath (shape);
        }
        g.setColour (clip ? col::amber : col::neon.withAlpha (juce::jlimit (0.0f, 1.0f, 0.25f + 0.75f * l)));
        g.strokePath (shape, juce::PathStrokeType (testing ? 2.6f : 2.0f));

        // Input channel number(s) inside the speaker
        const float fh = s.h < 20.0f ? 13.0f : (s.w >= 40.0f ? 18.0f : 14.0f);
        g.setFont (monoFont (fh, true));
        g.setColour (clip ? col::amber : (l > 0.05f ? juce::Colour (0xfff2fff0) : col::sub));
        g.drawText (number, r.expanded (14.0f, 0.0f).toNearestInt(), juce::Justification::centred, false);

        // Position code below
        g.setFont (monoFont (13.0f, true));
        g.setColour (l > 0.05f ? col::text : col::sub);
        g.drawText (kPosCodes[(size_t) p], juce::Rectangle<float> (r.getCentreX() - 40.0f, r.getBottom() + 4.0f, 80.0f, 16.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }

    AtmosBridgeProcessor& proc;
    std::array<float, kNumChannels> level {};
    std::array<int, kNumChannels> clipHold {};
};

//==============================================================================
// Window showing the hosted upmixer's own editor.
class HostedPluginWindow final : public juce::DocumentWindow
{
public:
    HostedPluginWindow (juce::AudioPluginInstance& p, std::function<void()> onCloseRequested)
        : juce::DocumentWindow (p.getName() + " (Atmos Bridge)", col::bg, juce::DocumentWindow::closeButton),
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
    explicit MapPanel (AtmosBridgeProcessor& p) : proc (p)
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

            auto& nb = noiseBtn[(size_t) i];
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
        resetBtn.setTooltip (tr ("채널 번호를 기본 순서(1~12)로 되돌립니다"));
        resetBtn.onClick = [this] { proc.resetMapping(); };
        addAndMakeVisible (resetBtn);

        allMuteBtn.onClick = [this] { proc.setAllMuted (! proc.areAllMuted()); };
        addAndMakeVisible (allMuteBtn);

        pickBtn.setButtonText (tr ("업믹서 선택\xe2\x80\xa6"));
        pickBtn.setTooltip (tr ("VST3 업믹서(예: zAuro Pro 2D-3D)를 Atmos Bridge 안에서 스테레오 \xe2\x86\x92 7.1.4로 엽니다"));
        pickBtn.onClick = [this] { showUpmixerMenu(); };
        addAndMakeVisible (pickBtn);

        openBtn.setButtonText (tr ("창 열기"));
        openBtn.setTooltip (tr ("업믹서 설정 창을 엽니다"));
        openBtn.onClick = [this] { if (onOpenUpmixerWindow) onOpenUpmixerWindow(); };
        addAndMakeVisible (openBtn);

        useBtn.setTooltip (tr ("켜면: 호스트 채널 1~2 \xe2\x86\x92 업믹서 \xe2\x86\x92 Ch 1~12 / 끄면: 호스트 채널 1~12를 그대로 사용"));
        useBtn.onClick = [this] { proc.setUpmixerEnabled (! proc.getUpmixer().isEnabled()); };
        addAndMakeVisible (useBtn);
    }

    std::function<void()> onOpenUpmixerWindow;

    static constexpr int kRowY0 = 114, kRowH = 35;

    void resized() override
    {
        resetBtn.setBounds (254, 18, 64, 30);
        allMuteBtn.setBounds (326, 18, 96, 30);
        for (int i = 0; i < kNumChannels; ++i)
        {
            const int y = kRowY0 + i * kRowH;
            numBox[(size_t) i].setBounds (76, y + 4, 52, 28);
            noiseBtn[(size_t) i].setBounds (230, y + 4, 30, 28);
            muteBtn[(size_t) i].setBounds (272, y + 4, 30, 28);
        }
        const int uy = kRowY0 + kNumChannels * kRowH + 8;
        pickBtn.setBounds (18, uy, 118, 28);
        openBtn.setBounds (142, uy, 78, 28);
        useBtn.setBounds (226, uy, 70, 28);
    }

    void refresh()
    {
        for (int i = 0; i < kNumChannels; ++i)
        {
            const auto m = proc.takeInputMeter (i);
            rms[(size_t) i] = juce::jmax (m.rms, rms[(size_t) i] * 0.85f);

            auto& cb = numBox[(size_t) i];
            const int wantId = proc.getPosition (i) + 2;
            if (cb.getSelectedId() != wantId && ! cb.isPopupActive())
                cb.setSelectedId (wantId, juce::dontSendNotification);
            cb.setColour (juce::ComboBox::outlineColourId,
                          proc.isDuplicatePosition (proc.getPosition (i)) ? col::amber : col::btnBorder);

            auto& nb = noiseBtn[(size_t) i];
            const bool nOn = proc.getInputNoise() == i;
            if (nb.active != nOn) { nb.active = nOn; nb.repaint(); }

            auto& mb = muteBtn[(size_t) i];
            const bool muted = proc.isMuted (i);
            mb.setColour (juce::TextButton::buttonColourId, muted ? col::muteBg : col::btn);
            mb.setColour (juce::TextButton::textColourOffId, muted ? col::muteFg : col::dim);
            setBorder (mb, muted ? col::muteBdr : col::btnBorder);
        }

        const bool all = proc.areAllMuted();
        allMuteBtn.setButtonText (all ? "All Unmute" : "All Mute");
        allMuteBtn.setColour (juce::TextButton::buttonColourId, all ? col::muteBg : col::btn);
        allMuteBtn.setColour (juce::TextButton::textColourOffId, all ? col::muteFg : col::text);
        setBorder (allMuteBtn, all ? col::muteBdr : col::btnBorder);
        allMuteBtn.repaint();
        for (auto& mb : muteBtn) mb.repaint();

        const auto& up = proc.getUpmixer();
        const bool loaded = up.getInfo().loaded;
        const bool on = loaded && up.isEnabled();
        openBtn.setEnabled (loaded);
        useBtn.setEnabled (loaded);
        useBtn.setButtonText (on ? tr ("사용 중") : tr ("꺼짐"));
        useBtn.setColour (juce::TextButton::buttonColourId, on ? col::neon.withAlpha (0.16f) : col::btn);
        useBtn.setColour (juce::TextButton::textColourOffId, on ? col::neon : col::dim);
        setBorder (useBtn, on ? col::neon : col::btnBorder);
        useBtn.repaint();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (col::text);
        g.setFont (uiFont (18.0f, true));
        g.drawText (tr ("채널 매핑"), 18, 16, 200, 22, juce::Justification::centredLeft);
        g.setColour (col::sub);
        g.setFont (uiFont (14.5f));
        g.drawText (tr ("입력 채널마다 출력 위치 번호를 지정합니다. 0 = 끄기"), 18, 40, 300, 18, juce::Justification::centredLeft, true);

        bool anyDup = false;
        for (int i = 0; i < kNumChannels; ++i) anyDup = anyDup || proc.isDuplicatePosition (proc.getPosition (i));
        if (anyDup)
        {
            juce::Rectangle<float> banner (18, 64, 404, 26);
            g.setColour (juce::Colour (0xff2a2210)); g.fillRoundedRectangle (banner, 6);
            g.setColour (juce::Colour (0xff5a4818)); g.drawRoundedRectangle (banner, 6, 1);
            g.setColour (col::amber); g.setFont (uiFont (14.5f));
            g.drawText (tr ("같은 번호가 중복되어 해당 위치에 합산됩니다."), banner.reduced (10, 0).toNearestInt(), juce::Justification::centredLeft);
        }

        g.setColour (col::sub);
        g.setFont (uiFont (14.0f));
        g.drawText (tr ("입력"), 18, 94, 50, 16, juce::Justification::centredLeft);
        g.drawText (tr ("번호"), 76, 94, 56, 16, juce::Justification::centredLeft);
        g.drawText (tr ("출력 위치"), 140, 94, 80, 16, juce::Justification::centredLeft);
        g.drawText (tr ("노이즈"), 226, 94, 38, 16, juce::Justification::centred);
        g.drawText (tr ("뮤트"), 268, 94, 38, 16, juce::Justification::centred);
        g.drawText (tr ("레벨"), 312, 94, 60, 16, juce::Justification::centredLeft);

        for (int i = 0; i < kNumChannels; ++i)
        {
            const int y = kRowY0 + i * kRowH;
            const bool muted = proc.isMuted (i);
            const float alpha = muted ? 0.55f : 1.0f;
            g.setColour (juce::Colour (0xff1f2529));
            g.drawHorizontalLine (y, 18.0f, 422.0f);

            g.setColour (col::text.withMultipliedAlpha (alpha));
            g.setFont (monoFont (15.0f));
            g.drawText ("Ch " + juce::String (i + 1), 18, y, 56, kRowH, juce::Justification::centredLeft);

            const int pos = proc.getPosition (i);
            const bool dup = proc.isDuplicatePosition (pos);
            g.setFont (monoFont (15.0f, true));
            g.setColour ((pos < 0 ? col::sub : col::text).withMultipliedAlpha (alpha));
            const juce::String target = pos >= 0 ? juce::String (kPosCodes[(size_t) pos]) : tr ("끄기");
            g.drawText (target, 140, y, 44, kRowH, juce::Justification::centredLeft);
            if (dup)
            {
                juce::Rectangle<float> tag (184, (float) y + 11, 34, 15);
                g.setColour (juce::Colour (0xff5a4818)); g.drawRoundedRectangle (tag, 4, 1);
                g.setColour (col::amber); g.setFont (uiFont (12.0f));
                g.drawText (tr ("중복"), tag.toNearestInt(), juce::Justification::centred);
            }

            // Meter
            const float l = displayLevel (rms[(size_t) i]);
            juce::Rectangle<float> bar (312, (float) y + 14, 60, 8);
            g.setColour (col::field); g.fillRoundedRectangle (bar, 4);
            if (l > 0.0f)
            {
                g.setColour (col::neon.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, 60.0f * l)), 4);
            }
            g.setColour (col::sub);
            g.setFont (monoFont (13.0f));
            g.drawText (dbText (rms[(size_t) i]), 374, y, 48, kRowH, juce::Justification::centredRight);
        }

        // Built-in upmixer status
        const int uy = kRowY0 + kNumChannels * kRowH;
        g.setColour (juce::Colour (0xff1f2529));
        g.drawHorizontalLine (uy, 18.0f, 422.0f);
        const auto& info = proc.getUpmixer().getInfo();
        g.setFont (uiFont (14.0f, true));
        g.setColour (info.loaded ? col::text : col::sub);
        g.drawText (info.loaded ? info.name : tr ("업믹서 없음"), 302, uy + 8, 120, 28, juce::Justification::centredLeft, true);

        juce::String detail;
        juce::Colour detailCol = col::sub;
        if (info.error.isNotEmpty())
        {
            detail = info.error;
            detailCol = col::amber;
        }
        else if (info.loaded)
        {
            detail = info.layout + tr (" \xc2\xb7 지연 ") + juce::String (1000.0 * info.latencySamples / juce::jmax (1.0, proc.getHostSampleRate()), 1) + " ms"
                   + (info.namedLayout ? tr (" \xc2\xb7 채널 순서 자동") : tr (" \xc2\xb7 번호 직접 지정"));
            if (! proc.getUpmixer().isEnabled()) detail = tr ("꺼짐 \xc2\xb7 ") + detail;
        }
        else
        {
            detail = tr ("호스트 채널 1~12를 그대로 사용합니다");
        }
        g.setColour (detailCol);
        g.setFont (uiFont (13.0f));
        g.drawText (detail, 18, uy + 40, 404, 20, juce::Justification::centredLeft, true);
    }

    void showUpmixerMenu()
    {
        installed = UpmixerHost::findInstalledVst3();
        juce::PopupMenu m;
        m.addSectionHeader (tr ("설치된 VST3"));
        for (int i = 0; i < installed.size(); ++i)
            m.addItem (i + 1, installed[i].getFileNameWithoutExtension(), true,
                       installed[i].getFullPathName() == proc.getUpmixer().getInfo().path);
        if (installed.isEmpty())
            m.addItem (-1, tr ("(VST3 폴더에서 찾지 못함)"), false);
        m.addSeparator();
        m.addItem (1000, tr ("다른 위치에서 찾기\xe2\x80\xa6"));
        if (proc.getUpmixer().getInfo().loaded)
            m.addItem (1001, tr ("업믹서 제거"));

        juce::Component::SafePointer<MapPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pickBtn), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 1001) { safe->proc.unloadUpmixer(); return; }
            if (r == 1000) { safe->browseForUpmixer(); return; }
            if (r - 1 < safe->installed.size()) safe->loadFrom (safe->installed[r - 1]);
        });
    }

    void browseForUpmixer()
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("업믹서 VST3 선택"),
                                                       juce::File ("C:\\Program Files\\Common Files\\VST3"), "*.vst3");
        juce::Component::SafePointer<MapPanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f != juce::File()) safe->loadFrom (f);
                              });
    }

    void loadFrom (const juce::File& f)
    {
        juce::MouseCursor::showWaitCursor();
        const bool ok = proc.loadUpmixer (f.getFullPathName());
        juce::MouseCursor::hideWaitCursor();
        if (ok && onOpenUpmixerWindow) onOpenUpmixerWindow();
    }

private:
    AtmosBridgeProcessor& proc;
    std::array<juce::ComboBox, kNumChannels> numBox;
    std::array<NoiseButton, kNumChannels> noiseBtn;
    std::array<juce::TextButton, kNumChannels> muteBtn;
    juce::TextButton resetBtn, allMuteBtn, pickBtn, openBtn, useBtn;
    std::array<float, kNumChannels> rms {};
    juce::Array<juce::File> installed;
    std::unique_ptr<juce::FileChooser> chooser;
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
    explicit RootView (AtmosBridgeProcessor& p) : proc (p), room (p), map (p)
    {
        addAndMakeVisible (room);
        addAndMakeVisible (map);

        map.onOpenUpmixerWindow = [this] { openUpmixerWindow(); };
        proc.getUpmixer().beforeUnload = [this] { upmixerWindow.reset(); };

        static const char* modeNames[kNumModes] = { "음악", "저지연", "초저지연" };
        static const char* modeTips[kNumModes] = {
            "음악 모드: 여유 버퍼 100 ms (가장 안정적)",
            "저지연 모드: 여유 버퍼 20 ms, 끊기면 10 ms씩 자동으로 늘림 (최대 +60 ms)",
            "초저지연 모드: 여유 버퍼 3 ms (최소), 자동 보정 없음 · 끊김은 횟수로만 표시"
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

        setSize (kBaseWidth, kBaseHeight);
    }

    ~RootView() override
    {
        proc.getUpmixer().beforeUnload = nullptr;
        upmixerWindow.reset();
    }

    void openUpmixerWindow()
    {
        if (upmixerWindow != nullptr) { upmixerWindow->toFront (true); return; }
        if (auto* inst = proc.getUpmixer().getInstance())
        {
            juce::Component::SafePointer<RootView> safe (this);
            upmixerWindow = std::make_unique<HostedPluginWindow> (*inst, [safe]
            {
                // Deleting the window from inside its own callback: defer to the next message.
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->upmixerWindow.reset(); });
            });
        }
    }

    void resized() override
    {
        device.setBounds (404, 34, 190, 32);
        for (int m = 0; m < kNumModes; ++m)
            modeBtn[(size_t) m].setBounds (1037 + m * 68, 37, 64, 30);
        room.setBounds (20, 96, 780, 620);
        map.setBounds (820, 96, 440, 620);
        seqBtn.setBounds (40, 744, 136, 36);
        stopBtn.setBounds (184, 744, 104, 36);
    }

    void refresh()
    {
        room.refresh();
        map.refresh();

        const int mode = (int) proc.getMode();
        for (int m = 0; m < kNumModes; ++m)
        {
            auto& b = modeBtn[(size_t) m];
            const bool on = m == mode;
            b.setColour (juce::TextButton::buttonColourId, on ? col::neon.withAlpha (0.16f) : juce::Colours::transparentBlack);
            b.setColour (juce::TextButton::textColourOffId, on ? col::neon : col::dim);
            setBorder (b, juce::Colours::transparentBlack);
            b.repaint();
        }

        const bool seq = proc.isSequenceOn();
        seqBtn.setButtonText (seq ? tr ("순차 테스트 중지") : tr ("순차 테스트 톤"));
        setBorder (seqBtn, seq ? col::neon : col::btnBorder);
        seqBtn.repaint();

        if (! device.isPopupActive())
        {
            const auto want = proc.getOutputDeviceId();
            const int idx = deviceIds.indexOf (want);
            if (idx >= 0 && device.getSelectedItemIndex() != idx)
                device.setSelectedItemIndex (idx, juce::dontSendNotification);
        }

        // Status (header pill + tooltip)
        updateStatus();
        repaint (0, 0, kBaseWidth, 96);
        repaint (0, 716, kBaseWidth, 96);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);

        // Header
        juce::Rectangle<float> header (20, 20, 1240, 60);
        g.setColour (col::panel); g.fillRoundedRectangle (header, 10);
        g.setColour (col::border); g.drawRoundedRectangle (header.reduced (0.5f), 10, 1);

        drawLogo (g, { 40.0f, 37.0f, 26.0f, 26.0f });
        g.setColour (col::text);
        g.setFont (uiFont (21.0f, true));
        g.drawText (JucePlugin_Name, 78, 28, 320, 24, juce::Justification::centredLeft);
        g.setColour (col::sub);
        g.setFont (uiFont (14.0f));
        g.drawText (tr ("Windows Spatial Audio \xe2\x86\x92 Dolby Atmos (MAT)"), 78, 52, 320, 16, juce::Justification::centredLeft);

        // Status pill
        juce::Rectangle<float> pill (604, 34, 310, 32);
        g.setColour (statusColour.withAlpha (0.10f)); g.fillRoundedRectangle (pill, 16);
        g.setColour (statusColour.withAlpha (0.45f)); g.drawRoundedRectangle (pill.reduced (0.5f), 16, 1);
        g.setColour (statusColour); g.fillEllipse (pill.getX() + 13, pill.getCentreY() - 4, 8, 8);
        g.setColour (col::text); g.setFont (uiFont (15.5f));
        g.drawText (statusText, pill.withTrimmedLeft (28).withTrimmedRight (10).toNearestInt(), juce::Justification::centredLeft, true);

        // Rate pill
        juce::Rectangle<float> rate (924, 34, 104, 32);
        const bool rateOk = std::abs (proc.getHostSampleRate() - 48000.0) < 1.0;
        g.setColour (juce::Colour (0xff1b2024)); g.fillRoundedRectangle (rate, 16);
        g.setColour (rateOk ? juce::Colour (0xff2a3136) : col::amber); g.drawRoundedRectangle (rate.reduced (0.5f), 16, 1);
        g.setColour (rateOk ? col::text : col::amber); g.setFont (monoFont (14.0f));
        g.drawText (rateText, rate.toNearestInt(), juce::Justification::centred);

        // Mode group background
        juce::Rectangle<float> grp (1034, 34, 206, 36);
        g.setColour (col::field); g.fillRoundedRectangle (grp, 10);
        g.setColour (juce::Colour (0xff2a3136)); g.drawRoundedRectangle (grp.reduced (0.5f), 10, 1);

        // Footer
        juce::Rectangle<float> footer (20, 732, 1240, 60);
        g.setColour (col::panel); g.fillRoundedRectangle (footer, 10);
        g.setColour (col::border); g.drawRoundedRectangle (footer.reduced (0.5f), 10, 1);

        g.setColour (col::sub); g.setFont (uiFont (15.0f));
        g.drawText (testText, 298, 744, 220, 36, juce::Justification::centredLeft, true);

        g.setColour (col::sub); g.setFont (uiFont (15.5f));
        g.drawText (modeText, 512, 744, 400, 36, juce::Justification::centredRight, true);

        g.setColour (dropouts > 0 ? col::amber : col::sub);
        g.drawText (tr ("끊김 ") + juce::String (dropouts), dropoutArea.toNearestInt(), juce::Justification::centred);

        g.setColour (clockColour); g.fillEllipse (1012, 758, 8, 8);
        g.setColour (col::text); g.setFont (uiFont (15.0f));
        g.drawText (clockText, 1026, 744, 222, 36, juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (dropoutArea.contains (e.position))
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
        juce::String tip;
        if (! proc.isOutputOwner())
        {
            statusColour = col::sub;
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
                        statusColour = col::amber;
                        statusText = tr ("가상 케이블로 출력 중: 장치를 AVR로 선택하세요");
                    }
                    else
                    {
                        statusColour = col::neon;
                        statusText = tr ("출력 중 \xc2\xb7 ") + name;
                    }
                    break;
                case EngineStatus::Connecting:
                    statusColour = col::amber;
                    statusText = tr ("연결 중\xe2\x80\xa6");
                    break;
                case EngineStatus::SpatialOff:
                    statusColour = col::amber;
                    statusText = tr ("공간 음향 꺼짐: Dolby Atmos for home theater 선택");
                    break;
                case EngineStatus::NoDevice:
                    statusColour = col::red;
                    statusText = tr ("출력 장치 없음");
                    break;
                case EngineStatus::Error:
                    statusColour = col::red;
                    statusText = tr ("오류 (마우스를 올려 자세히 보기)");
                    break;
                case EngineStatus::Unsupported:
                    statusColour = col::sub;
                    statusText = tr ("Windows 전용");
                    break;
                case EngineStatus::Stopped:
                default:
                    statusColour = col::sub;
                    statusText = tr ("정지");
                    break;
            }

            const double outRate = s->engine.getOutputRate();
            rateText = juce::String (outRate / 1000.0, outRate == std::floor (outRate / 1000.0) * 1000.0 ? 0 : 1) + tr (" kHz \xc2\xb7 12ch");

            const bool running = st == EngineStatus::Running;
            static const char* modeNames[kNumModes] = { "음악 모드", "저지연 모드", "초저지연 모드" };
            const int mode = (int) proc.getMode();
            modeText = tr (modeNames[mode]) + tr (" \xc2\xb7 여유 버퍼 ") + juce::String (juce::roundToInt (s->feeder.getMarginMs()))
                     + tr (" ms \xc2\xb7 플러그인 지연 ")
                     + (running && s->feeder.getState() == OutputFeeder::State::Running
                            ? juce::String (s->feeder.getLatencyMs() + proc.getUpmixerLatencyMs(), 1) + " ms" : juce::String ("-- ms"))
                     + (proc.getUpmixerLatencyMs() > 0.0 ? tr (" (업믹서 포함)") : juce::String());
            dropouts = (int) s->feeder.getDropouts();

            const float drift = std::abs (s->feeder.getDriftPpm());
            if (! running)                          { clockColour = col::sub;   clockText = tr ("출력 대기"); }
            else if (! s->feeder.isProducerAlive()) { clockColour = col::sub;   clockText = tr ("입력 신호 없음 (호스트 정지?)"); }
            else if (s->feeder.isProducerBursty())  { clockColour = col::amber; clockText = tr ("호스트 처리 불규칙"); }
            else if (drift > 300.0f)                { clockColour = col::amber; clockText = tr ("클럭 차이 큼: 호스트 출력=AVR 권장"); }
            else                                    { clockColour = col::neon;  clockText = tr ("클럭: 호스트와 동기 정상"); }
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

    static void drawLogo (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float s = r.getWidth() / 24.0f;
        auto P = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * s, r.getY() + y * s); };
        juce::Path p;
        p.startNewSubPath (P (4, 9)); p.lineTo (P (4, 15)); p.lineTo (P (8, 15)); p.lineTo (P (13, 19));
        p.lineTo (P (13, 5)); p.lineTo (P (8, 9)); p.closeSubPath();
        juce::Path w1; w1.addCentredArc (r.getX() + 12 * s, r.getY() + 12 * s, 4 * s, 4 * s, 0, 0.6f, 2.55f, true);
        juce::Path w2; w2.addCentredArc (r.getX() + 11 * s, r.getY() + 12 * s, 8 * s, 8 * s, 0, 0.75f, 2.4f, true);
        g.setColour (col::neon);
        const juce::PathStrokeType st (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.strokePath (p, st); g.strokePath (w1, st); g.strokePath (w2, st);
    }

    // Invisible component over the status pill that carries its tooltip.
    struct TipArea final : public juce::Component, public juce::SettableTooltipClient
    {
        TipArea() { setInterceptsMouseClicks (true, false); }
    };

public:
    void addTipArea()
    {
        statusTip.setBounds (604, 34, 310, 32);
        addAndMakeVisible (statusTip);
    }

private:
    AtmosBridgeProcessor& proc;
    RoomView room;
    MapPanel map;
    DeviceBox device;
    juce::StringArray deviceIds;
    std::array<juce::TextButton, kNumModes> modeBtn;
    juce::TextButton seqBtn, stopBtn;
    TipArea statusTip;
    juce::String lastTip;

    std::unique_ptr<HostedPluginWindow> upmixerWindow;

    juce::Colour statusColour { col::sub }, clockColour { col::sub };
    juce::String statusText, rateText { tr ("48 kHz \xc2\xb7 12ch") }, modeText, testText, clockText;
    int dropouts = 0;
    const juce::Rectangle<float> dropoutArea { 920.0f, 744.0f, 84.0f, 36.0f };
};

//==============================================================================
AtmosBridgeEditor::AtmosBridgeEditor (AtmosBridgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    lnf = std::make_unique<NeonLookAndFeel>();
    setLookAndFeel (lnf.get());

    root = std::make_unique<RootView>(proc);
    root->addTipArea();
    addAndMakeVisible (*root);

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
        c->setSizeLimits (kBaseWidth / 2, kBaseHeight / 2, (int) (kBaseWidth * 1.5), (int) (kBaseHeight * 1.5));
    }
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

void AtmosBridgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
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
    if (root != nullptr) root->refresh();
}
