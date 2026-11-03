// Order in which ParameterRegistry::Process fires its handlers: it iterates a std::unordered_map, so the
// order is the standard library's, not the registration order. Mirrors DroseraCore's registration.
#include <cstdio>
#include "ParameterRegistry.h"
enum Param : int { Frequency = 0, FeedbackGain, FeedbackBody, FeedbackLPF, FeedbackHPF, ReverbMix, ReverbDecay, EchoSend, EchoTime, EchoFeedback, OutputVolume, NumParams };
static const char* names[] = {"Frequency", "FeedbackGain", "FeedbackBody", "FeedbackLPF", "FeedbackHPF", "ReverbMix", "ReverbDecay", "EchoSend", "EchoTime", "EchoFeedback", "OutputVolume"};
int main() {
    infrasonic::ParameterRegistry<Param> r; r.Init(12000.0f);
    for (int p = 0; p < NumParams; ++p) r.Register((Param) p, 0.5f, 0.0f, 1.0f, [p](float) { std::printf("%s ", names[p]); });
    std::printf("registration order: Frequency .. OutputVolume\nhandler order:      "); r.Process(); std::printf("\n");
    return 0;
}
