#include <cstdio>
#include "DroseraCore.h"
int main() { std::printf("sizeof Engine %zu KB, FeedbackVoice %zu KB, ReverbSc %zu KB, StereoEcho %zu KB, DroseraCore %zu KB\n",
  sizeof(infrasonic::FeedbackSynth::Engine)/1024, sizeof(infrasonic::FeedbackSynth::FeedbackVoice)/1024, sizeof(daisysp::ReverbSc)/1024,
  sizeof(infrasonic::StereoEcho<48000*5>)/1024, sizeof(infrasonic::DroseraCore)/1024); }
