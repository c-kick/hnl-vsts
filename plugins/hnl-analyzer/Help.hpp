// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Help texts for the UI: what each part of the window shows or does, in plain words.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kMode:
        return "What is measured. Track: one pass of the song, from the moment the host starts playing until it stops, kept afterwards. Continuous: the meter runs all the time, whatever the transport does, until Reset, and shows the last minutes. Both measure the sound that passes through the plugin, which is never changed. A sound file is not measured here but taken as a reference, in the Reference panel.";
    case kAuto:
        return "On: a capture begins when the host starts playing and ends when it stops, and the result stays. A jump of the transport (a loop, a click in the song) ends the capture as incomplete and begins a new one. Off: Start and Stop begin and end a capture by hand.";
    case kRmsWindow:
        return "The length of the stretches the RMS figures are taken over, in milliseconds: Maximum, Minimum and Average RMS and the histogram are of windows of this length, one after the other. All six lengths are measured at once, so this can be changed after a capture. Adobe Audition uses 50 ms unless told otherwise.";
    case kRmsRef:
        return "What counts as 0 dB for the RMS figures. Sine: a sine wave at full scale reads 0 dB, as in Adobe Audition's Amplitude Statistics unless told otherwise. Square: a square wave at full scale does, which reads 3.01 dB lower. Loudness and peaks are not affected.";
    case kTargetLufs: return "The integrated loudness to aim for, in LUFS. Streaming services play at about -14; a louder master is turned down by them. The green line in the loudness history is this target.";
    case kMaxTruePeak: return "The highest true peak the master may have, in dBTP. -1 leaves room for the encoding a streaming service does; -2 is safer for loud masters.";
    case kPrefPlr: return "The peak to loudness ratio that is usual for this kind of music: how far the highest peak stands above the integrated loudness. The advisor remarks on a master that lies more than 1 dB from it. It does not change the gain figures.";
    default: return "";
    }
}

const char* const kLoudness = "The four figures of the whole track. Integrated: its loudness from start to end, with silence and quiet passages left out as the standard says (ITU-R BS.1770, EBU R128). True peak: the highest level between the samples as well, red when it is over the ceiling of the advisor. PLR: true peak minus integrated loudness. LRA: the loudness range, how far the quiet and the loud parts lie apart.";
const char* const kCapture = "How the measurement stands. A green dot: a result that is whole. Blue: being measured now. Red: cut short, with the reason beside it. The result is kept with the song.";
const char* const kStart = "Begins a capture by hand, or ends the one that is running. A capture begun by hand ignores the transport: it runs until Stop.";
const char* const kReset = "Forgets the result that is shown and starts the meter again. The capture before it stays available as a reference.";
const char* const kName = "The name of the track: click and type, Enter to keep it. It goes into the reports and is the name the track is saved under as a reference.";
const char* const kReport = "The figures of this window as a report. Copy puts them on the clipboard as text; TXT, MD and JSON write a file, plain, as Markdown or as data. The advisor and the comparison with the reference are in it.";
const char* const kWave = "The whole track: the outline of left and right, and under it the loudness over the same time. The bright line is short-term loudness (the last 3 s), the field behind it momentary (0.4 s), the green dashes the target, the yellow line the reference. In Continuous mode it shows the last minutes.";
const char* const kStatistics = "The figures of Adobe Audition's Amplitude Statistics, per channel. Peak is the largest sample, true peak the largest value between samples. Total RMS is over the whole track; Maximum, Minimum and Average RMS are of windows of the chosen length. Crest factor is peak minus total RMS. Clipped samples are flat tops at full scale. Correlation below zero means left and right cancel in mono.";
const char* const kHistogram = "How often each RMS level occurred, for windows of the chosen length, both channels together: loud at the right, 60 dB across. A narrow hill is a track at one level throughout; a wide one has quiet and loud parts. The yellow line is the reference.";
const char* const kSpectrum = "The mean spectrum of the whole track in third octaves, from 20 Hz at the left to 20 kHz at the right, 60 dB from bottom to top. The yellow line is the reference: where it lies above, the reference has more in that range.";
const char* const kAdvisor = "What it takes to reach the targets. Required gain: to reach the loudness target. Maximum gain under the ceiling: how much gain the peaks allow. Minimum peak reduction: what the gain cannot do, and a limiter or compressor must. The advice below says which of the two is needed. It only reads the measurement and changes nothing.";
const char* const kPresets = "Sets of targets for the advisor: by service, by kind of music. The arrows step through them. A * after the name means a target was changed since the preset was chosen. Save as... at the end of the list keeps the targets of this moment under a name of your own; the x behind such a preset deletes it.";
const char* const kReference = "The track beside a reference: its figures, the reference's, and the difference (track minus reference). The reference is also drawn in yellow in the loudness history, the histogram and the spectrum.";
const char* const kReferenceChoice = "Which reference to compare with: none, the capture before this one (to see what a change did), or one of the saved references. A sound file added with + FILE is measured and becomes one of them.";
const char* const kMatch = "How the reference is set against the track. As measured: as it is. Loudness: the reference moved to the track's integrated loudness, so that the difference in peaks and dynamics shows without the difference in level. True peak: moved to the track's true peak, so that the difference in loudness at the same ceiling shows.";
const char* const kReferenceKeys = "+ TRACK keeps the analysis that is shown as a reference, under the track's name. + FILE takes a WAV, AIFF or FLAC file as the reference: a released song, an earlier master. It is measured much faster than it plays, saved under the file's name and compared with at once; while it is read this key cancels. The same key takes a reference exported as JSON, and a file can also be dropped on the window. EXPORT writes the chosen reference to a JSON file, DELETE removes it after asking once. References are files in the HNL folder and hold for every song.";
const char* const kHelp = "Help is on: rest the mouse on a part of the window to read what it shows or does. Click here again to switch it off.";

} // namespace help
