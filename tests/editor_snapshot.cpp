// Renders the plugin editor to a PNG: editor_snapshot <out.png> [paramId=normalizedValue | program=N | note=N | tab=N | sound=1 ...]
// tab opens a tab. sound=1 renders a second of audio with the window open, so the plant shows real sound.
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: editor_snapshot <out.png> [id=value ...]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    int tab = -1;
    bool sound = false, prepared = false;
    auto prepare = [&] { if (!prepared) { proc->prepareToPlay(48000.0, 512); prepared = true; } };
    for (int i = 2; i < argc; ++i) {
        juce::String arg(argv[i]);
        auto id = arg.upToFirstOccurrenceOf("=", false, false);
        auto v = arg.fromFirstOccurrenceOf("=", false, false).getFloatValue();
        if (id == "program") { proc->setCurrentProgram((int) v); continue; }
        if (id == "tab") { tab = (int) v; continue; }
        if (id == "sound") { sound = v > 0.5f; continue; }
        if (id == "note") {  // play a note through processBlock before drawing
            prepare();
            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, (int) v, (juce::uint8) 100), 0);
            proc->processBlock(buf, midi);
            continue;
        }
        for (auto* p : proc->getParameters())
            if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                if (wp->paramID == id) wp->setValueNotifyingHost(v);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    auto* editor = dynamic_cast<DroseraEditor*>(ed.get());
    ed->setVisible(true);
    if (tab >= 0) editor->setTab(tab);
    if (prepared || sound) {  // let the values settle, with the window open
        prepare();
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer none;
        for (int b = 0; b < 90; ++b) {  // a block is about 11 ms, and the display needs a tick for every 33 ms
            buf.clear();
            proc->processBlock(buf, none);
            if (b % 3 == 2) editor->refreshDisplay(0.032f);
        }
    }
    editor->refreshDisplay(0.0f);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(argv[1]);
    out.deleteFile();
    juce::FileOutputStream fos(out);
    juce::PNGImageFormat().writeImageToStream(img, fos);
    std::printf("editor %dx%d tab %d -> %s\n", ed->getWidth(), ed->getHeight(), editor->tab(), argv[1]);
    return 0;
}
