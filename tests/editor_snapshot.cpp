// Renders the plugin editor to a PNG: editor_snapshot <out.png> [paramId=normalizedValue | program=N | note=N ...]
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: editor_snapshot <out.png> [id=value ...]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    for (int i = 2; i < argc; ++i) {
        juce::String arg(argv[i]);
        auto id = arg.upToFirstOccurrenceOf("=", false, false);
        auto v = arg.fromFirstOccurrenceOf("=", false, false).getFloatValue();
        if (id == "program") { proc->setCurrentProgram((int) v); continue; }
        if (id == "note") {  // play a note through processBlock before drawing
            proc->prepareToPlay(48000.0, 512);
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
    ed->setVisible(true);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(argv[1]);
    out.deleteFile();
    juce::FileOutputStream fos(out);
    juce::PNGImageFormat().writeImageToStream(img, fos);
    std::printf("editor %dx%d -> %s\n", ed->getWidth(), ed->getHeight(), argv[1]);
    return 0;
}
