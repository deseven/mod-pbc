#ifndef MOD_PBC_EVENT_PROCESSOR_H
#define MOD_PBC_EVENT_PROCESSOR_H

#include <ctime>
#include <string>
#include <vector>
#include "ObjectGuid.h"
#include "pbc_config.h"

struct PBC_EventItem;

// ---------------------------------------------------------------------------
// Narrator segment parsing helpers
// ---------------------------------------------------------------------------
struct NarratorSegment {
    std::string text;
    bool isNarrator; // true if wrapped in *asterisks*
};

std::vector<NarratorSegment> ParseNarratorSpans(const std::string& text);
void PushReplySegments(const PBC_CharacterSnapshot& snap,
                       PBC_EventItem& ev,
                       const std::vector<NarratorSegment>& segments);
std::string AdjustWhisperPerspective(const std::string& text,
                                     const std::string& fromPerspective,
                                     const std::string& toPerspective);

// ---------------------------------------------------------------------------
// Process a single event item (runs in a detached thread).
// ---------------------------------------------------------------------------
void PBC_ProcessEventItem(PBC_EventItem ev);

// ---------------------------------------------------------------------------
// Push a narrator summary as a PendingAction to real players in anchor's group.
// ---------------------------------------------------------------------------
void PBC_PushNarratorSummary(const ObjectGuid& anchorObjGuid, const std::string& eventLine);

#endif // MOD_PBC_EVENT_PROCESSOR_H
