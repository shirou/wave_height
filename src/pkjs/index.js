/*
 * Copyright 2026 The wave_height Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Phone-side placeholder.
 *
 * Raw logging currently goes through the DataLogging API, which buffers on the
 * watch and forwards on the next phone connection -- exactly what a sea trial
 * needs, since there is no phone in range out there. If DataLogging turns out
 * not to work on this firmware, the fallback is to stream samples here over
 * app_message and buffer them on the phone; persist cannot stand in, as a
 * single segment is many times PERSIST_DATA_MAX_LENGTH.
 *
 * Nothing is wired up until that is known, so this only reports readiness.
 */

Pebble.addEventListener('ready', function () {
  console.log('wave_height: phone side ready');
});

Pebble.addEventListener('appmessage', function (e) {
  // Reserved for the app_message logging fallback described above.
  console.log('wave_height: message ' + JSON.stringify(e.payload));
});
