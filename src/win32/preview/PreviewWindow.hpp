#pragma once

#include "ClickPulse.hpp"
#include "PaintSurface.hpp"
#include "Theme.hpp"
#include "WindowPickerDialog.hpp"
#include "preview/ControlIds.hpp"
#include "triggerlab/AnalyzerSettings.hpp"
#include "triggerlab/ColorProfile.hpp"
#include "triggerlab/HoverClickGate.hpp"
#include "triggerlab/ScreenCapture.hpp"
#include "triggerlab/TrainingProfile.hpp"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace triggerlab::win32 {

class PreviewWindow {
  public:
    ~PreviewWindow();
    explicit PreviewWindow(const std::filesystem::path &dataDirectory = {},
                           triggerlab::ClickPulse::Sender sender = SendInput);
    PreviewWindow(const PreviewWindow &) = delete;
    PreviewWindow &operator=(const PreviewWindow &) = delete;
    bool createWindow(HINSTANCE instance);
    void show(int command);
    int messageLoop();

  private:
    friend struct PreviewTestHarness;
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp);
    HFONT uiFont = nullptr, titleFont = nullptr, headingFont = nullptr, controlFont = nullptr;
    UINT dpi = 96;
    int scrollX = 0, scrollY = 0, wheelRemainder = 0;
    bool arranging = false;
    struct ControlRect {
        HWND control;
        RECT logical;
        RECT placed{};
    };

#ifdef TRIGGERLAB_UI_TESTS
    bool allowOversizedTestWindow = false;
#endif
    bool narrow = false;
    int canvasWidth = 1102, canvasHeight = 950, headerHeight = 144;
    HWND headerStatus = nullptr, headerBackdrop = nullptr, qualityInfo = nullptr, homeHint = nullptr,
         homeSummary = nullptr;
    bool homeDetails = false;
    enum class SnapshotPurpose { Color, Target, Background };
    SnapshotPurpose snapshotPurpose = SnapshotPurpose::Color;
    std::optional<triggerlab::Frame> targetReference, backgroundReference;
    HWND referenceWindow = nullptr;
    DWORD referenceProcess = 0;
    RECT windowCard{}, runCard{}, optionsCard{}, colorCard{}, qualityCard{}, stateCard{}, previewCards[2]{};
    int footerY = 0;
    std::vector<ControlRect> controlRects;
    HBRUSH cardBrush = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH inputBrush = CreateSolidBrush(RGB(248, 250, 252));
    HWND tooltip = nullptr;

    triggerlab::ScreenCapture capture;
    triggerlab::CaptureSample sample;
    triggerlab::ColorMask mask;
    std::optional<triggerlab::Frame> displayFrame;
    triggerlab::ColorMask displayMask;
    triggerlab::AnalyzerSettings displaySettings;
    ULONGLONG displayTime = 0;
    bool displayFound = false;
    triggerlab::ColorMatchConfig color;
    triggerlab::AnalyzerSettings settings;
    std::filesystem::path settingsPath;
    std::array<HWND, 8> settingEdits{};
    std::array<HWND, 8> settingLabels{};
    std::array<HWND, 8> settingHelp{};
    HWND colorHelp = nullptr;
    static const wchar_t *parameterHelp(int index);
    std::array<HWND, 4> colorLabels{};
    HWND settingsHint = nullptr, colorHint = nullptr, profileChoice = nullptr, profileName = nullptr;
    bool uiSync = false, uiReady = false, colorReady = false, verified = false, profilePane = false;
    bool paramsOnDisk = false, colorOnDisk = false;
    triggerlab::AnalyzerSettings savedSettings;
    triggerlab::ColorMatchConfig savedColor;
    std::vector<triggerlab::NamedProfile> profiles;
    std::wstring selectedProfile;
    std::wstring validationMessage;
    std::wstring actionError;
    enum class FeedbackTone { Neutral, Info, Good, Waiting, Error };
    struct Feedback {
        FeedbackTone tone;
        std::wstring title, detail;
    };
    std::optional<triggerlab::TrainingProfile> profileBaseline;
    bool syntheticDown = false;
    triggerlab::ClickPulse inputPulse;
    using PressTiming = triggerlab::ClickPulse::Result;
    std::optional<PressTiming> lastPress;
    HWND modeControl = nullptr, dryControl = nullptr;
    std::optional<triggerlab::CapturePoint> analysisPoint;
    std::optional<triggerlab::Region> analyzedClient;
    std::uint64_t diagnosticCount = 0;
    std::uint64_t runFrames = 0, runAlignedFrames = 0, runUnavailableFrames = 0;
    double runBestCoverage = 0;
    std::wstring runLastIssue;
    ULONGLONG lastPaint = 0;
    std::unique_ptr<PaintSurface> backBuffer;
    int bufferWidth = 0, bufferHeight = 0;
    HWND window = nullptr, targetWindow = nullptr, autoControl = nullptr;
    bool running = false, autoClick = false;
    DWORD targetProcess = 0;
    std::wstring targetTitle;
    std::array<HWND, 4> colorEdits{};
    triggerlab::HoverClickGate gate;
    std::uint64_t clickCount = 0;
    std::wstring clickStatus = L"Выключен";
    ULONGLONG pickDeadline = 0, targetDeadline = 0, aimPickDeadline = 0;
    std::filesystem::path colorPath;
    std::optional<triggerlab::Frame> importedImage;

    void applyTheme();
    bool inputAuthorized() const;
    void syncActions();
    bool persistSettings(std::string &error);
    static bool fieldIs(HWND field, int value);
    bool settingsDirty() const;
    static bool sameColor(const triggerlab::ColorMatchConfig &a,
                          const triggerlab::ColorMatchConfig &b) noexcept;
    bool colorDirty() const;
    bool profileChanged() const;
    bool readDraft(triggerlab::AnalyzerSettings &next, triggerlab::ColorMatchConfig &nextColor);
    bool chosenColor(const triggerlab::ColorMatchConfig &draft) const;
    bool saveAll();
    void onEditChange(HWND field);
    static std::wstring wide(const std::string &text);
    void refreshProfiles();
    void saveProfile();
    void loadProfile();
    int scaled(int value) const;
    void mapCanvas(HDC dc, int x = 0, int y = 0) const;
    void placeLogical(HWND control, int x, int y, int width, int height);
    bool pinned(HWND child) const;
    void placeHeader();
    void narrowControls();
    void homeControls();
    void arrange();
    void setDpi(UINT value);
    void reveal(HWND control);
    void scroll(int bar, int request);
    static LRESULT CALLBACK viewportProcedure(HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                              DWORD_PTR context);
    static LRESULT CALLBACK headerProcedure(HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                            DWORD_PTR context);
    void layoutMode();
    int wizardStep() const;
    bool colorFound() const;
    std::wstring saveState() const;
    static std::wstring percent(double fraction);
    void clearReferences();
    bool referencesCurrent() const;
    std::wstring qualityText() const;
    bool recordReference(triggerlab::CaptureSample fresh, SnapshotPurpose purpose);
    std::wstring captureHint() const;
    std::wstring detectionHint() const;
    std::wstring nextHint() const;
    Feedback feedback(bool foreground, bool held) const;
    static std::wstring captureHintForBlack();
    Feedback currentFeedback() const;
    COLORREF feedbackColor(FeedbackTone tone) const;
    void updateWizard();
    void guidedAction();
    std::optional<triggerlab::Region> clientBounds() const;
    void syncSettings();
    bool applySettings();
    void findTrainer(const wchar_t *name);
    void syncColor();
    std::wstring pressTimingText() const;
    void finishClick(bool count = true);
    void disableClicks();
    void cancelSelection();
    void beginAimSelection(SnapshotPurpose purpose = SnapshotPurpose::Color);
    void finishAimSelection();
    bool applyColor();
    void chooseColor();
    void saveColor();
    bool importImage(const std::filesystem::path &path, bool edit = false);
    void editImage();
    void chooseImage();
    void refreshTargetButton();
    bool applyWindowChoice(const triggerlab::WindowChoice &choice);
    void chooseTargetWindow();
    void beginSelection(bool pick);
    void selectionTick(ULONGLONG now);
    void finishTargetSelection();
    bool permittedAt(POINT point) const;
    bool permittedRegion(triggerlab::Region region) const;
    void handleClick(POINT capturedCursor);
    static bool inputHeld(bool includeLeftButton = true);
    void createControls();
    void command(int id, int notification = BN_CLICKED);
    std::wstring notice;
    std::wstring status = L"Захват остановлен";
    void stop();
    void update(bool pick = false);
    void acceptSample(triggerlab::CaptureSample fresh, bool pick);

    static LRESULT CALLBACK checkProcedure(HWND control, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                           DWORD_PTR context);
    void drawCheck(HWND control, HDC dc);
    void drawButton(DRAWITEMSTRUCT item);
    void paintHome(HDC dc);
    void paintNarrow(HDC dc);
    void paintHeader(HDC dc, int clientWidth);
    void paint(HDC target, RECT rect);
};

} // namespace triggerlab::win32
