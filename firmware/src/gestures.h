#pragma once

// Touch gestures on the panel (touch boards only; main.cpp turns them on when
// BoardCaps.has_touch). A press is classified on release:
//   tap               on the meters: director_tap (he runs off, no face)
//   swipe up          on the meters: the face (director_face_open), the only way it rises
//   swipe right→left  on the meters: the sessions page (sessions_view.h)
//   swipe left→right  on the sessions page: back to the meters
//                     on the meters: the colony page (colony_view.h)
//   swipe right→left  on the colony page: back to the meters
// On the sessions page vertical drags scroll the list and never open the face.
void gestures_init(void);
