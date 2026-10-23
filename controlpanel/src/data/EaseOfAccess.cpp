#include "EaseOfAccess.h"

const QList<DetailGroup> &easeOfAccessGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "preferences-desktop-accessibility", "Ease of Access Center",
            {
                { "Let Linux suggest settings",
                  "Optimize visual display",
                  "Replace sounds with visual cues" },
                { "Change how your mouse works",
                  "Change how your keyboard works" },
            }
        },
        {
            "audio-input-microphone", "Speech Recognition",
            {
                { "Start speech recognition",
                  "Set up a microphone" },
            }
        },
    };
    return groups;
}
