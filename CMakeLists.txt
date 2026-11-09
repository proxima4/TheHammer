cmake_minimum_required(VERSION 3.22)

project(TheHammer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # Static runtime so the plugins don't need the VC++ redistributable installed
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local ./JUCE checkout if present, otherwise download it.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(HAMMER_BUILD_TOOLS "Build the offline render / self-test tool" ON)

set(HAMMER_FORMATS VST3)
if(APPLE)
    list(APPEND HAMMER_FORMATS AU)
endif()

# ---------------------------------------------------------------------------
# Embedded neural brain (optional). The train-brain workflow commits
# model/hammer_brain.json; if it exists it is baked into the plugins.
# ---------------------------------------------------------------------------
set(HAMMER_BRAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/model/hammer_brain.json")
if(EXISTS "${HAMMER_BRAIN_FILE}")
    message(STATUS "The Hammer: embedding trained brain ${HAMMER_BRAIN_FILE}")
    juce_add_binary_data(HammerBrainData SOURCES "${HAMMER_BRAIN_FILE}")
    set_target_properties(HammerBrainData PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(HAMMER_HAS_BRAIN 1)
else()
    message(STATUS "The Hammer: no trained brain found - plugins will use the Playbook improviser until one is trained")
    set(HAMMER_HAS_BRAIN 0)
endif()

# ---------------------------------------------------------------------------
# Shared engine sources (compiled into every target)
# ---------------------------------------------------------------------------
set(HAMMER_ENGINE_SOURCES
    Source/Common/HammerTypes.cpp
    Source/Ears/OnsetAnalyzer.cpp
    Source/Ears/ListeningState.cpp
    Source/Ears/SharedLink.cpp
    Source/Brain/StyleLibrary.cpp
    Source/Brain/PlaybookEngine.cpp
    Source/Brain/NeuralEngine.cpp
    Source/Brain/FillSoloGenerator.cpp
    Source/Brain/Conductor.cpp
    Source/Sound/SynthVoices.cpp
    Source/Sound/SampleKit.cpp
    Source/Sound/DrumKitEngine.cpp
    Source/Engine/HammerEngine.cpp
    Source/Engine/MidiRecorder.cpp
    Source/Plugin/HammerParams.cpp
)

set(HAMMER_GUI_COMMON
    Source/GUI/RoadCaseLookAndFeel.cpp
    Source/GUI/HammerComponents.cpp
)

function(hammer_common_setup target)
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        HAMMER_HAS_BRAIN=${HAMMER_HAS_BRAIN}
        HAMMER_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(${target} PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
    if(HAMMER_HAS_BRAIN)
        target_link_libraries(${target} PRIVATE HammerBrainData)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 1) The Hammer - the drummer instrument (Channel Rack)
# ---------------------------------------------------------------------------
juce_add_plugin(TheHammer
    COMPANY_NAME "APT Industrial"
    COMPANY_WEBSITE "https://github.com/proxima4"
    PLUGIN_MANUFACTURER_CODE Aptx
    PLUGIN_CODE Hmr1
    PRODUCT_NAME "The Hammer"
    BUNDLE_ID com.aptindustrial.thehammer
    FORMATS ${HAMMER_FORMATS} Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum
    DESCRIPTION "Intelligent session drummer by APT Industrial")
target_sources(TheHammer PRIVATE
    ${HAMMER_ENGINE_SOURCES} ${HAMMER_GUI_COMMON}
    Source/GUI/HammerEditor.cpp
    Source/Plugin/HammerPluginBase.cpp
    Source/Plugin/DrummerProcessor.cpp)
target_compile_definitions(TheHammer PRIVATE HAMMER_MODE_DRUMMER=1)
hammer_common_setup(TheHammer)

# ---------------------------------------------------------------------------
# 2) The Hammer FX - single mixer-insert mode (listens to its input/sidechain)
# ---------------------------------------------------------------------------
juce_add_plugin(TheHammerFX
    COMPANY_NAME "APT Industrial"
    PLUGIN_MANUFACTURER_CODE Aptx
    PLUGIN_CODE Hmr2
    PRODUCT_NAME "The Hammer FX"
    BUNDLE_ID com.aptindustrial.thehammerfx
    FORMATS ${HAMMER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Drum
    DESCRIPTION "The Hammer drummer as a mixer insert - hears the track it sits on")
target_sources(TheHammerFX PRIVATE
    ${HAMMER_ENGINE_SOURCES} ${HAMMER_GUI_COMMON}
    Source/GUI/HammerEditor.cpp
    Source/Plugin/HammerPluginBase.cpp
    Source/Plugin/MixerProcessor.cpp)
target_compile_definitions(TheHammerFX PRIVATE HAMMER_MODE_MIXER=1)
hammer_common_setup(TheHammerFX)

# ---------------------------------------------------------------------------
# 3) The Hammer Ears - pass-through listener that feeds the drummer
# ---------------------------------------------------------------------------
juce_add_plugin(TheHammerEars
    COMPANY_NAME "APT Industrial"
    PLUGIN_MANUFACTURER_CODE Aptx
    PLUGIN_CODE Hmr3
    PRODUCT_NAME "The Hammer Ears"
    BUNDLE_ID com.aptindustrial.thehammerears
    FORMATS ${HAMMER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer
    DESCRIPTION "Listens to your song and tells The Hammer what it hears")
target_sources(TheHammerEars PRIVATE
    ${HAMMER_ENGINE_SOURCES} ${HAMMER_GUI_COMMON}
    Source/GUI/EarsEditor.cpp
    Source/Plugin/EarsProcessor.cpp)
target_compile_definitions(TheHammerEars PRIVATE HAMMER_MODE_EARS=1)
hammer_common_setup(TheHammerEars)

# ---------------------------------------------------------------------------
# Offline renderer / self-test (renders demo WAVs, checks the brain)
# ---------------------------------------------------------------------------
if(HAMMER_BUILD_TOOLS)
    juce_add_console_app(HammerRender PRODUCT_NAME "HammerRender")
    target_sources(HammerRender PRIVATE ${HAMMER_ENGINE_SOURCES} Tools/HammerRender.cpp)
    target_compile_definitions(HammerRender PRIVATE HAMMER_MODE_TOOL=1)
    hammer_common_setup(HammerRender)
endif()

# GUI snapshot tool (renders the editors to PNG for review) - developer only
option(HAMMER_BUILD_SNAPSHOT "Build the GUI snapshot tool" OFF)
if(HAMMER_BUILD_SNAPSHOT)
    juce_add_console_app(HammerSnapshot PRODUCT_NAME "HammerSnapshot")
    target_sources(HammerSnapshot PRIVATE ${HAMMER_ENGINE_SOURCES} ${HAMMER_GUI_COMMON}
        Source/GUI/HammerEditor.cpp Source/GUI/EarsEditor.cpp
        Source/Plugin/HammerPluginBase.cpp Source/Plugin/DrummerProcessor.cpp Source/Plugin/EarsProcessor.cpp
        Tools/HammerSnapshot.cpp)
    target_compile_definitions(HammerSnapshot PRIVATE HAMMER_MODE_TOOL=1 HAMMER_SNAPSHOT_TOOL=1 JucePlugin_Name="The Hammer")
    hammer_common_setup(HammerSnapshot)
    target_link_libraries(HammerSnapshot PRIVATE juce::juce_gui_extra)
endif()
