# Built-in demonstrations

The emulator includes all 22 demos in the 2dfx 1.1.4 command set. This is the
complete catalogue, including platform restrictions and interactive controls.
Guest-command layouts are in [COMMAND_REFERENCE.md](COMMAND_REFERENCE.md).

## Selection, timing and input

Wake the device with SET_ENGINE_MODE, then write a selector to F8h without
parameters. Selection waits for a safe scene boundary and for accepted
resource jobs to finish. Each demo sets up its own scene. Resource RAM is
retained across selection, and a demo can overwrite the previous application's
resources. SYS_RESET stops the demo and restores application defaults.

CFh and D0h run only with the TVC profile. An unavailable selector, including
D6h..DFh or CFh/D0h on Enterprise, stops demo updates while retaining the
current graphics and resources.

Keep supplying HSYNC and 50 Hz VSYNC callbacks. Animation uses accepted scene
updates, not host wall-clock time. Durations below assume 50 accepted updates
per second. Portable support provides logical time and deterministic entropy.

Only C7h, C8h and D5h consume interactive input. Send their control bytes to
F9h while the command parser is idle. A byte inside a command packet or upload
is still that transaction's data. Keyboard or joystick mapping belongs to the
host application; the module does not install keyboard bindings.

## Complete catalogue

| Opcode | Demonstration |
|---|---|
| C0 | Animated 2dfx logo |
| C1 | Sprite benchmark |
| C2 | Drawing primitives |
| C3 | Text |
| C4 | Animated bitmaps |
| C5 | Pattern fill |
| C6 | Tilemap |
| C7 | Basic Pong |
| C8 | Pong showcase |
| C9 | Wriggler sprites |
| CA | Boulder scrolling |
| CB | ARC/PIE benchmark |
| CC | Graphics diagnostics |
| CD | Full-area bitmap |
| CE | Drawing layer order |
| CF | TVC colour-bit patterns, TVC only |
| D0 | TVC tearing diagnostic, TVC only |
| D1 | Coordinate ruler |
| D2 | XY offsets |
| D3 | Elite ship showcase |
| D4 | Six-scene 2D graphics demonstration |
| D5 | Public-mesh tunnel |

All selector values in the table are hexadecimal. Except for CFh/D0h, every
demo supports both Enterprise and TVC.

### C0h — Animated 2dfx logo

Striped letter sprites begin scattered, assemble into the title, flip,
settle and finish with a changing rainbow treatment. The scene also uses the
built-in 2dfx logo and a collision indicator. TVC includes an orange checker
treatment. It demonstrates sprite movement, transforms, layering and colour
changes.

### C1h — Sprite benchmark

Two 540×230 image panels are loaded from compressed assets. After an
approximately ten-second introduction, small smileys appear one per second
until 62 are present. The scene exercises sprite transforms and then animates
all 64 sprites: small sprites wrap and the two large sprites bounce.

### C2h — Drawing primitives

Coloured bands, a line grid, ellipses and rounded rectangles combine with
erased lines and bucket-filled regions. The scene shows outlined and filled
primitives, ink/paper behaviour and commands replayed into the same layer.

### C3h — Text

Compressed fonts are decoded and used at 8×8, 16×8, 32×8, 16×16 and 32×16
sizes. Text uses both retained and paper-filled backgrounds, alongside a
changing frame counter. It exercises font descriptors, glyph layout and
transparent paper.

### C4h — Animated bitmaps

Four 128×64 animated objects use banks of 64 frames loaded from compressed
assets. Animation changes the resource address in each bitmap descriptor,
allowing fixed drawing commands to display changing frames. It exercises
resource loading, DEFINE_BITMAP, BLIT and descriptor publication.

### C5h — Pattern fill

Eight generated patterns cover large and small panels. A highlighted ticker
changes more quickly than the background. The scene demonstrates repeated
16×8 patterns, different pattern indices and overlapping patterned regions.

### C6h — Tilemap

A 16×12-cell map uses 16×8 tiles, producing a 256×96-pixel map image. A moving
marker, frame and bars accompany it. The scene demonstrates map and tileset
definitions, tile indices and composition with other drawing commands.

### C7h — Basic Pong

Two paddles and a ball use three sprites, with seven-segment scores and a
score-triggered shake effect. Idle-F9h input controls both paddles and starts
a new game. See the common Pong controls below.

### C8h — Pong showcase

An expanded Pong presentation uses tilemap, pattern, font, string and sprite
assets together. It uses the same controls as C7h while demonstrating a scene
built from several resource types.

### C9h — Wriggler sprites

Up to 63 normal worms appear in stages, with body colours from indices 1..15.
A giant worm uses sprite 63 and enters later. Prepared image sizes combine
with sprite doubling and flipping, exercising priority, transparency and a
crowded sprite scene.

### CAh — Boulder scrolling

A scripted camera route scrolls across a Boulder-style tilemap with a player
sprite. It alternates pauses and smooth movement and visits map edges and
outside regions, including the right and bottom. There is no player input.

### CBh — ARC/PIE benchmark

A fixed workload combines two ellipses, six filled pie sectors and eight
arcs. It exercises curved primitives, angular ranges and overlaps. Host
execution time is not a measurement of 2dfx hardware rendering time.

### CCh — Graphics diagnostics

Five stages run for 1,000 accepted updates each, approximately twenty seconds
at 50 fps: resource address/cache behaviour, sprite transforms, clipping,
mixed resource types and ZX1 decompression, in that order.

### CDh — Full-area bitmap

Two stages run for 1,000 accepted updates each. The first draws a large
bitmap with BLIT; the second combines it with sprites. It exercises large
bitmap coverage and bitmap/sprite layer interaction.

### CEh — Drawing layer order

Four moving sprites appear between a background grid and foreground bars
and frames. The expected order is background 2DPT, sprites, foreground 2DPT.
An incorrect layer order or transparency rule is readily visible.

### CFh — TVC colour-bit patterns

TVC only. Sixteen horizontal bands, fifteen lines high, encode their colour
index as four repeating black/white pixels corresponding to bits 3..0.
These are bit-pattern bands, not solid-colour bars. On 2dfx hardware they
help inspect colour-bit sampling and alignment. The emulator supplies the
intended image without simulating electrical signal timing.

### D0h — TVC tearing diagnostic

TVC only. Two approximately twenty-five-second phases alternate: full-area
dark-red/cyan flashing, then a moving black/white boundary. The boundary moves
in sixteen-pixel steps between raw X positions 80 and 688. This gives a clear
target for frame continuity and host composition; the emulator does not
manufacture electrical tearing.

### D1h — Coordinate ruler

A bright marker sweeps X in both directions, then Y in both directions,
pausing at the ends. Neighbouring pixels and a seven-segment coordinate
readout help locate it. This probes raw coordinates, including positions
outside the host window: Enterprise X reaches 820 while the normal visible
right edge is 811. Do not crop internal coordinates to the host window.

### D2h — XY offsets

The C2h primitive composition moves horizontally, vertically and diagonally
through SET_XY_OFFSET. Geometry stays the same while the retained offset
changes, exercising consistent translation and framebuffer-edge clipping.

### D3h — Elite ship showcase

Cobra, Asp, Sidewinder, Viper and Krait appear in sequence, each for 1,500
accepted updates, approximately thirty seconds. The showcase prepares its
own models and emits 2D drawing commands; it does not directly exercise the
public DEFINE_MESH/DRAW_MESH command path. Complete scene preparation
precedes publication.

### D4h — Six-scene 2D graphics demonstration

The six scenes run in this order, each for 1,000 accepted updates
(approximately twenty seconds at 50 fps):

| Scene | Content and expected behaviour |
|---:|---|
| 1 | Four concave arrow polygons use PATTERN_BUCKET_FILL. Top left: local origin, opaque. Top right: screen origin, opaque. Bottom left: local origin, transparent. Bottom right: screen origin, transparent. The fill follows the polygon interiors. |
| 2 | CROP_BLIT selects bitmap regions and draws them at native size. |
| 3 | STRETCH_BLIT scales selected bitmap regions with nearest-neighbour sampling. |
| 4 | A mixed drawing scene uses the full framebuffer clip. |
| 5 | The same mixed scene uses a fixed clip rectangle. |
| 6 | The clip rectangle shrinks and expands on an approximately five-second cycle. |

There is no pointer demo selector. Applications use the global
SET_POINTER_MODE, SET_POINTER_POS, SET_POINTER_REL and DEFINE_USER_POINTER
commands described in the command reference.

### D5h — Public-mesh tunnel

A curved tunnel and an Asp ship with exhaust exercise the public mesh,
projection, camera and mesh-drawing functions. It starts in wireframe mode
with twelve forward rings, fast travel, the ship visible and varying ship
distance enabled. Complete scene preparation precedes publication.

## C7h/C8h Pong controls

Each idle-F9h byte replaces the input state. Queued states are consumed and
the latest is used for that update. Held bits persist until a new byte changes
them; send 00h to release all controls.

| Bit / mask | Meaning |
|---|---|
| 7 / 80h | New game on a released-to-pressed transition |
| 3 / 08h | Left paddle up |
| 2 / 04h | Left paddle down |
| 1 / 02h | Right paddle up |
| 0 / 01h | Right paddle down |
| 6..4 | Unused |

Combine held controls with bitwise OR. A new-game release and subsequent
press must be observed on separate demo updates; sending both before the
same update can collapse them into the final state.

## D5h controls

Each input byte is a command, not a held-key bitmask. Unsupported values have
no effect. Ring counts below are forward rings; one additional near guard
ring gives total geometry counts of five, nine or thirteen.

| F9h byte | Effect |
|---|---|
| 00h, 01h, 02h | Wireframe with 4, 8 or 12 forward rings respectively |
| 03h, 04h, 05h | Filled mode with 4, 8 or 12 forward rings respectively |
| F0h, F1h, F2h | Slow, medium or fast travel respectively |
| F3h | Show the ship |
| F4h | Hide the ship |
| F5h | Disable changing ship distance; hold a fixed distance ahead of the camera |
| F6h | Enable changing ship distance |
| FFh | Cycle through all six ring modes, changing every 400 accepted updates (approximately eight seconds at 50 fps) |

Selecting 00h..05h stops automatic cycling. Speed, ship visibility and
ship-distance controls do not select a different ring mode.

## Integration and asset scope

Demo logic and assets follow the 2dfx 1.1.4 scenes. Per-module state codecs
preserve each device instance and save-state continuation. Input mapping,
display scaling and final palette conversion remain host responsibilities.
Diagnostic scenes do not synthesize electrical tearing or hardware render
overruns. Third-party-derived artwork remains subject to
[ASSET_LICENSES.md](../ASSET_LICENSES.md).
