# 2dfx 1.1.4 command reference

This is the complete current guest-command reference for emulator integrators. It covers sprites, global controls, every implemented resource and 2DPT command, 3D, overlays and demo selection. Host callback wiring is in [INTEGRATION.md](INTEGRATION.md); migration instructions are separate in [UPGRADING.md](UPGRADING.md). Command names use the public 2dfx names throughout.

Write the command byte to F8h and its parameters, in the order shown, to F9h. Hexadecimal command numbers below have an `h` suffix. Resource addresses are three bytes, low byte first; bit 7 of the third byte is ignored. Resource RAM contains 8 MiB. Ordinary upload and pixel reads wrap at its end; a mesh descriptor must fit entirely in it.

## Startup and status

After a physical reset, send `50h` followed by a nonzero engine mode. Before this transaction, other commands are ignored. The portable device completes wake-up synchronously. Read F8h bit 6 until it is zero before sending normal commands in software intended for 2dfx hardware.

| F8h status bit | Meaning when zero |
|---|---|
| 7 | Collision in the previously completed frame |
| 6 | Device initialized and ready |
| 5 | Sticky resource FIFO overflow; no execution-duration overruns are simulated |
| 4, 3, 2, 1 | Raster interrupt 4, 3, 2, 1 pending |
| 0 | Hardware: render activity / SYS_RESET busy. Emulator: remains one because rendering and reset finish synchronously within host calls |

Collision is a two-horizontal-pixel cell approximation between enabled, collision-enabled sprites with overlapping bounding boxes. It is evaluated in the stored framebuffer, including pixels outside the host's visible window. The completed result appears at the next VSYNC and is retained across skipped frames.

An upload transaction (`80h..82h`, including its header) or UNZX1 argument transaction (`83h..85h`) ignores ordinary F8h commands. `8Fh SYS_RESET` can abort it. A completed `8Eh SHOW_FW` command enters a terminal information display: all subsequent F8h/F9h writes, including SYS_RESET, are ignored until a physical reset.

## Command families and parameter notation

| F8h opcode | Scope |
|---|---|
| `00h..3Fh` | Sprite selection and sequential SPB writes |
| `40h..4Fh` | Global transparent-colour index |
| `50h..58h` | Global engine, cadence, status and raster controls |
| `5Bh..5Fh` | Resource definitions |
| `60h..7Fh` | Background 2DPT slots |
| `80h..88h` | Upload, decompression, mesh definition, global projection/camera |
| `8Eh..95h` | System commands and global logo/pointer controls |
| `C0h..DFh` | Built-in demo selection |
| `E0h..FFh` | Foreground 2DPT slots |

Each F9h count excludes the F8h opcode. Drawing-command counts include the
slot byte. `address[3]` is a 23-bit little-endian address; `X[2]` denotes a
little-endian 16-bit value. Signed bytes use two's complement. Unless specified
otherwise, X/width have 10 bits (0..1023), Y/height have 9 bits (0..511), and
dimensions are literal counts. Reserved high bits are ignored.

Global commands execute outside the two 2DPTs. Commands listed under the
2DPT section write retained slots, including their own SET_COLOR,
SET_XY_OFFSET and SET_CLIP_RECT replay state. Pointer commands are global:
`94h` is **SET_POINTER_REL**, with no slot parameter.

Soft sprite, global-parameter and 2DPT input sessions can be replaced by a new
F8h command. A global/2DPT command commits only after all of its parameters
arrive; an interrupted partial packet leaves that command's existing state
unchanged. Sprite bytes are the exception: each received SPB byte updates its
shadow byte immediately. Strict upload/UNZX1 sessions follow the rules above.

## Global commands and resources

| Command | F9h bytes | Parameters / behaviour |
|---|---:|---|
| `00h..3Fh` | up to 9 | Select sprite 0..63 and write its SPB sequentially; partial updates are valid, additional bytes after byte 9 are ignored until another command |
| `40h..4Fh` | 0 | Set transparent colour from the command's low nibble |
| `50h` SET_ENGINE_MODE | 1 | 0=off, 1..254=normal, 255=forced |
| `51h` SET_FPS | 1 | 1=25 fps; all other values=50 fps; video callbacks remain 50 Hz |
| `52h` SET_RENDER_OVERRUN_VISUAL | 1 | Zero=off, nonzero=on; displays the shared sticky overrun condition |
| `53h` CLEAR_RENDER_OVERRUN | 0 | Clear overrun status |
| `54h` SET_RASTER_INT1 | 2 | Comparator 1 line, **high byte first**: 0 disables, 1..311 enables, other values disable; clears its pending latch |
| `55h` SET_RASTER_INT2 | 2 | Comparator 2 line, same encoding and behaviour |
| `56h` SET_RASTER_INT3 | 2 | Comparator 3 line, same encoding and behaviour |
| `57h` SET_RASTER_INT4 | 2 | Comparator 4 line, same encoding and behaviour |
| `58h` CLEAR_RASTER_INT | 0 | Clear all four pending latches |
| `5Bh` DEFINE_FONT | 5 | id, address[3], size |
| `5Ch` DEFINE_BITMAP | 7 | id, address[3], WH high bits, W low, H low |
| `5Dh` DEFINE_PATTERN | 4 | bank, address[3] |
| `5Eh` DEFINE_TILESET | 4 | bank, address[3] |
| `5Fh` DEFINE_TILEMAP | 7 | id, address[3], columns−1, rows−1, tileset |
| `80h` UPLOAD_RAW | 5 + data | address[3], length low, length high, payload; zero length means 65536; stores bytes unchanged |
| `81h` UPLOAD_NICK | 5 + data | Same header and length rule; converts NICK-format pixel bytes |
| `82h` UPLOAD_TVC | 5 + data | Same header and length rule; converts TVC-format pixel bytes |
| `83h` UNZX1_RAW | 6 | source address[3], destination address[3]; decoded bytes unchanged |
| `84h` UNZX1_NICK | 6 | source address[3], destination address[3]; decoded NICK pixels converted |
| `85h` UNZX1_TVC | 6 | source address[3], destination address[3]; decoded TVC pixels converted |
| `86h` DEFINE_MESH | 7 | id, address[3], vertices−1, triangle-count high, triangle-count low |
| `87h` SET_3D_PROJECTION | 8 | FX, FY, near, far: four unsigned 16-bit little-endian values |
| `88h` SET_CAMERA_POSE | 10 | X[2], Y[2], Z[2], angle high bits, roll low, yaw low, pitch low |
| `8Eh` SHOW_FW | 0 | Terminal 1.1.4 information screen |
| `8Fh` SYS_RESET | 0 | Reset application/resources while preserving engine mode |
| `90h` SHOW_BUILTIN_LOGO | 4 | XY high bits (bit 7=small), X low, Y low, main/accent colours |
| `91h` HIDE_BUILTIN_LOGO | 0 | Hide logo |
| `92h` SET_POINTER_MODE | 2 | mode, outer/inner colours |
| `93h` SET_POINTER_POS | 3 | XY high bits, X low, Y low |
| `94h` SET_POINTER_REL | 2 | signed 8-bit delta X, delta Y |
| `95h` DEFINE_USER_POINTER | 5 | address[3], hotspot X, hotspot Y |
| `C0h..D5h` | 0 | Start a built-in demo; CFh and D0h are TVC-only; see [EGGS.md](EGGS.md) |

Unassigned commands have no drawing or resource effect. An unavailable demo selector stops demo updates while retaining the current graphics and resources; D6h..DFh do not select demos.

### Engine, transparency, cadence and interrupts

`40h..4Fh` selects the global transparent index directly: `40h` means index 0,
`4Fh` means index 15, and no F9h byte follows. This selects an index, not an RGB
colour. The host emulator maps indices to its NICK palette/BIAS or TVC colours.

`50h SET_ENGINE_MODE` selects OFF, NORMAL or FORCED. OFF removes the overlay
without deleting resources, descriptors or drawing lists. Resource processing
and raster callbacks continue after wake-up. NORMAL supplies the 2dfx pixel
where its index differs from the transparent key. FORCED supplies the entire
stored 2dfx framebuffer, including key-coloured pixels. A zero mode while cold
does not complete the wake-up handshake.

`51h SET_FPS` changes scene acceptance to 25 fps for parameter 1, or 50 fps for
every other value. Changing it restarts the cadence phase. The host must still
call VSYNC at 50 Hz and HSYNC on every scanline. Pointer updates, demos and
retained scene publication follow the accepted scene cadence.

`52h SET_RENDER_OVERRUN_VISUAL` controls the flashing exclamation mark; it does
not acknowledge an existing error. `53h CLEAR_RENDER_OVERRUN` clears sticky
STATUS5 without cancelling accepted resource jobs. Resource FIFO overflow is
modelled; hardware execution-time and input-byte timing overruns are not.

`54h`, `55h`, `56h`, `57h` set raster comparators 1, 2, 3, 4 respectively.
They use raw host scanline numbers and unusually take the **high byte first**.
Writing a comparator clears its own pending latch. Reaching its enabled line
sets that latch; passing the line or reaching VSYNC does not clear it.
`58h CLEAR_RASTER_INT` acknowledges all four. The host IRQ is the OR of the
four pending latches; it remains asserted until acknowledged. These callbacks
and comparators operate independently of scene cadence.

### System lifecycle

`8Fh SYS_RESET` stops the selected demo, cancels pending jobs and incomplete
transactions, clears all 8 MiB of resource RAM and the displayed frame, and
restores application defaults. It preserves the selected OFF/NORMAL/FORCED
engine mode and the machine profile. Both 2DPTs have RET in every slot;
sprites and resource descriptors are disabled; raster and collision latches,
STATUS5, logo and pointer are cleared; transparent index is 0 and cadence is
50 fps. Projection resets to the defaults listed below. The device remains
awake. In the portable model reset completes synchronously; on 2dfx hardware
STATUS0 indicates the asynchronous reset interval.

`8Eh SHOW_FW` stops the application, cancels pending resource work and prepares
the 1.1.4 information page. The next VSYNC publishes it in FORCED mode. This is
terminal: even `SYS_RESET` is ignored until `twodfx_emu_power_cycle` or device
recreation. A physical reset returns to the cold wake gate.

`C0h..DFh` selects a demo; selection is deferred to a safe scene boundary.
The full catalogue, platform restrictions and idle-F9h controls are in
[EGGS.md](EGGS.md). An unsupported selection stops demo updates while keeping
the current scene and resources. Demo selection is not part of either 2DPT.

### Resource definitions and memory layout

Definitions hold metadata pointing into resource RAM; they neither upload nor
copy the referenced data. The complete descriptor becomes visible to drawing
with an accepted scene. Resource bytes themselves are shared and are not
snapshotted with a descriptor: overwriting a live resource changes what later
draws read. Undefined descriptors make their dependent draw commands do
nothing. Except for explicit address-boundary rules on meshes, resource reads
wrap modulo 8 MiB.

| Definition | IDs | Layout of the referenced resource |
|---|---|---|
| DEFINE_FONT | 0..63, high ID bits ignored | 128 glyphs in character-code order; each glyph has `height` rows of `width/8` bytes; bit7 is the leftmost pixel |
| DEFINE_BITMAP | 0..255 | Packed 4bpp rows, `ceil(width/2)` bytes per row |
| DEFINE_PATTERN | 0..63, high ID bits ignored | Up to 256 patterns; each 16×8 pattern occupies 64 bytes, row-major |
| DEFINE_TILESET | 0..63, high ID bits ignored | Up to 256 tiles; each 16×8 tile occupies 64 bytes, row-major |
| DEFINE_TILEMAP | 0..31, high ID bits ignored | Row-major bytes: one tile index per map cell; tileset ID uses its low six bits |
| DEFINE_MESH | 0..255 | Vertex and triangle arrays described in the 3D section |
| DEFINE_USER_POINTER | One global custom slot | 24×12 packed 4bpp pixels, 144 bytes, copied into the pointer cache when safe |

For DEFINE_FONT, `size = ((width/8-1)<<4) | (height/8-1)`.
Each nibble encodes an eight-pixel unit count minus one, giving dimensions
from 8 to 128 pixels.
For DEFINE_BITMAP, `WH = ((width>>8)<<1) | (height>>8)`; only bits2..0 are
used. Zero width or height makes the bitmap empty.
DEFINE_TILEMAP stores `columns-1` and `rows-1`, so a zero dimension byte
means **one cell**, unlike the literal viewport counts in the TILEMAP draw
command. Each map references a tileset descriptor; redefining that tileset
changes subsequent accepted scenes using the map.

### Upload and decompression

All three upload commands take the same five-byte header and exactly the
declared payload length. Data writes complete the transaction automatically;
extra idle F9h bytes are not part of the upload. Addresses advance and wrap
modulo 8 MiB. Use UPLOAD_RAW for compressed streams, fonts, strings, maps,
meshes and already-packed 2dfx pixels. UPLOAD_NICK and UPLOAD_TVC convert each
uncompressed source byte to the internal packed representation shown below.
They do not reinterpret font, mesh or text metadata.

UNZX1_RAW, UNZX1_NICK and UNZX1_TVC share the six-byte address packet and the
same FIFO. RAW leaves decoded bytes unchanged; NICK/TVC convert decoded
literal bytes and copy back-references from already-converted output. Choose
the variant from the source graphics format, independently of the selected
host machine. Compression does not change descriptor dimensions or addresses.

RAW graphics store the left pixel in the low nibble, the right pixel in the high nibble. Bitmap/sprite row stride is `(width+1)/2` bytes; odd widths do not carry the unused nibble into the next row. NICK and TVC conversion commands are available on both machine profiles. Both map source bits as follows:

| Result pixel | Index bit 3 | Index bit 2 | Index bit 1 | Index bit 0 |
|---|---:|---:|---:|---:|
| Left, stored in low nibble | source 1 | source 5 | source 3 | source 7 |
| Right, stored in high nibble | source 0 | source 4 | source 2 | source 6 |

Upload compressed streams with UPLOAD_RAW, then select the appropriate UNZX1 command for the decompressed source format. The dedicated FIFO accepts 16 outstanding jobs, counting the running job. Completed six-byte requests capture source, destination and conversion mode in arrival order. A full FIFO rejects only the new job and asserts sticky STATUS5; accepting the 16th job is not an overflow. Each HSYNC callback drains all accepted jobs; VSYNC also drains them as a fallback, skipping scene publication at that opportunity. Resource work continues with ENGINE_OFF and at either frame cadence. A failed decode does not discard subsequent jobs. `53h` clears STATUS5 without flushing the queue; draining alone does not clear it. SYS_RESET, power cycle and SHOW_FW cancel pending jobs; demo activation waits for them to finish.

Keep compressed source regions stable until consumed: the FIFO stores addresses, not copies of data. Independent `UPLOAD_RAW`, `UNZX1_*`, `DEFINE_BITMAP` sequences require no added delay or Z80 handshake. READY remains an awake/presence indication. A host must advance HSYNC during guest execution; feeding more than 16 requests without a service opportunity deliberately exercises overflow. Decompression writes in place, so overlapping source/destination regions and malformed streams can affect subsequent reads or leave partial output, as on 2dfx hardware.

## Sprite parameter block

| Byte | Meaning |
|---:|---|
| 0 | bit 7 enable, bit 6 collision enable, bits 5..4 X9..8, bit 3 Y8, bits 2..1 W9..8, bit 0 H8 |
| 1, 2 | X low, Y low |
| 3..5 | Resource address, little-endian |
| 6 | bit 7 double X, bit 6 double Y, bit 5 flip X, bit 4 flip Y; remaining bits ignored |
| 7, 8 | Source width low, source height low |

Zero width or height draws nothing. Transparent source pixels are skipped. Sprite 63 has the highest sprite priority. Rendering order is background 2DPT, sprites, foreground 2DPT, built-in logo, pointer, optional overrun marker.

## Retained 2D drawing tables

Background opcodes are `60h..7Fh`; foreground equivalents add `80h`. The first parameter is always the slot (0..255). Both tables reset with **RET in every slot**. A RET ends traversal; fill gaps with NOP when later slots must run. Complete entries become active at accepted frame boundaries. Each table starts with ink 15, paper equal to the transparent key, zero XY offset and the full framebuffer clip.

Unless stated otherwise, `H` packs X9..8 into bits 5..4, Y8 into bit 3, W9..8 (or X1 high) into bits 2..1, and H8 (or Y1 high) into bit 0. Commands supporting a stroke use bits 7..6 for thickness minus one (1..4). Geometry uses raw display coordinates. SET_XY_OFFSET adds signed 8-bit offsets to drawing positions; SET_CLIP_RECT remains in absolute raw coordinates.

The following parameter lists include the slot and give the complete F9h order.

| Background | Foreground | 2DPT command | F9h bytes | Parameters in wire order |
|---|---|---|---:|---|
| `60h` | `E0h` | NOP | 1 | slot |
| `61h` | `E1h` | SET_COLOR | 2 | slot, ink/paper |
| `62h` | `E2h` | PLOT | 4 | slot, H, X, Y |
| `63h` | `E3h` | LINE | 6 | slot, H, X0, Y0, X1, Y1 |
| `64h` | `E4h` | ERASE_LINE | 6 | slot, H, X0, Y0, X1, Y1 |
| `65h` | `E5h` | BUCKET_FILL | 4 | slot, H, X, Y |
| `66h` | `E6h` | RECT | 6 | slot, H, X, Y, W, Ht |
| `67h` | `E7h` | FILL_RECT | 6 | slot, H, X, Y, W, Ht |
| `68h` | `E8h` | ROUND_RECT | 8 | slot, H, X, Y, W, Ht, radius X, radius Y |
| `69h` | `E9h` | FILL_ROUND_RECT | 8 | slot, H, X, Y, W, Ht, radius X, radius Y |
| `6Ah` | `EAh` | ELLIPSE | 6 | slot, H, centre X, centre Y, radius X, radius Y |
| `6Bh` | `EBh` | FILL_ELLIPSE | 6 | slot, H, centre X, centre Y, radius X, radius Y |
| `6Ch` | `ECh` | ARC | 8 | slot, H, centre X, centre Y, radius X, radius Y, start, end |
| `6Dh` | `EDh` | PIE | 8 | slot, H, centre X, centre Y, radius X, radius Y, start, end |
| `6Eh` | `EEh` | SET_XY_OFFSET | 3 | slot, signed X offset, signed Y offset |
| `6Fh` | `EFh` | SKIP_NEXT | 2 | slot, count |
| `70h` | `F0h` | DRAW_TEXT | 8 | slot, H, X, Y, font, string address[3] |
| `71h` | `F1h` | BLIT | 5 | slot, H, X, Y, bitmap |
| `72h` | `F2h` | PATTERN_FILL | 8 | slot, H, X, Y, W, Ht, bank, pattern |
| `73h` | `F3h` | TILEMAP | 8 | slot, map/XY high bits, X, Y, source column, source row, columns, rows |
| `74h` | `F4h` | PATTERN_BUCKET_FILL | 6 | slot, H, seed X, seed Y, bank, pattern |
| `75h` | `F5h` | TRIANGLE | 9 | slot, H01, X0, Y0, X1, Y1, H2, X2, Y2 |
| `76h` | `F6h` | FILL_TRIANGLE | 9 | slot, H01, X0, Y0, X1, Y1, H2, X2, Y2 |
| `77h` | `F7h` | CROP_BLIT | 10 | slot, Hsource, source X, source Y, source W, source H, Hdest, dest X, dest Y, bitmap |
| `78h` | `F8h` | STRETCH_BLIT | 12 | slot, Hsource, source X, source Y, source W, source H, Hdest, dest X, dest Y, dest W, dest H, bitmap |
| `79h` | `F9h` | SET_CLIP_RECT | 6 | slot, H, X, Y, W, Ht |
| `7Ah` | `FAh` | DRAW_MESH_WIREFRAME | 12 | slot, X[2], Y[2], Z[2], angle high bits, roll low, yaw low, pitch low, mesh id |
| `7Bh` | `FBh` | DRAW_MESH_FILLED | 12 | slot, X[2], Y[2], Z[2], angle high bits, roll low, yaw low, pitch low, mesh id |
| `7Ch` | `FCh` | DRAW_MESH_FWE | 12 | slot, X[2], Y[2], Z[2], angle high bits, roll low, yaw low, pitch low, mesh id |
| `7Fh` | `FFh` | RET | 1 | slot |

`Ht` denotes the low height byte, to distinguish it from the packed high-bits
byte. Command-specific packing and behaviour are detailed below.

### Replay control: NOP, SET_COLOR, SET_XY_OFFSET, SKIP_NEXT, RET

NOP occupies a slot and continues. RET terminates the current layer, leaving
the other layer independent. Since every slot resets to RET, a missing slot
can hide all later commands. SKIP_NEXT advances past exactly its unsigned
count of following slots without executing them; even a skipped RET has no
effect. Traversal ends if the resulting next slot is beyond 255.

SET_COLOR takes `(ink<<4)|paper` and changes subsequent commands in this layer.
It does not change the host palette or the global transparent index.
SET_XY_OFFSET replaces the current signed destination offset; it is not an
increment. It translates both line endpoints and all triangle vertices, and
the destination positions of bitmap, text, pattern and tile commands. It does
not change widths, source rectangles, resource addresses or the global camera.
Offsets reset independently to zero at the start of each layer replay.

SET_CLIP_RECT replaces the current layer's clip with an absolute raw-coordinate
rectangle. XY offsets do not move it. Zero width or height gives an empty clip.
It affects subsequent 2DPT drawing, including flood traversal and mesh output,
but not sprites, logo or pointer. Both layers begin with the complete stored
framebuffer clip. A later full-size clip restores unrestricted drawing.

### Primitive drawing

PLOT writes one ink-coloured pixel. LINE draws between its two endpoints in
ink; ERASE_LINE uses paper with the same geometry. H bits7..6 encode stroke
width minus one for stroked primitives. These commands do not implicitly
modify the following slot's ink, paper or offset.

RECT and ROUND_RECT take a top-left position and literal width/height. Their
filled counterparts paint with ink. Outlined forms draw in ink and fill their
interior with paper only when paper differs from the global transparent key;
otherwise the existing interior is retained. ROUND_RECT adds unsigned 8-bit
corner radii X and Y. Zero rectangle dimensions draw nothing.

ELLIPSE and FILL_ELLIPSE take a centre and horizontal/vertical radii, not a
bounding-box corner and dimensions. ELLIPSE uses ink for its outline and the
same optional paper rule; FILL_ELLIPSE uses ink. ARC draws the angular portion
of an ellipse outline; PIE fills the sector in ink. Angles are unsigned 8-bit
clockwise values: 0=top, 64=right, 128=bottom, 192=left.
A start angle greater than the end angle
sweeps through zero; equal angles draw nothing. Radius fields use the same
10-bit/9-bit high-bit packing as W/H.

BUCKET_FILL replaces the four-connected region matching the seed's current
framebuffer colour with ink, bounded by the current clip. A seed outside the
clip, or ink already equal to the seed colour, does nothing. This is a
seed-colour fill, not a search for a specified border colour. Its bounded
128-seed traversal can leave a pathological region partially filled.

TRIANGLE has three full-range vertices. H01 packs X0/Y0/X1/Y1 like LINE and
contains stroke bits; H2 packs X2 high bits in bits5..4 and Y2 bit8 in bit3,
with other bits ignored. It draws all three edges in ink and
optionally fills with paper. Triangle outline widths are native framebuffer
pixels; even widths put the extra pixel on the negative side of the edge's
minor axis. FILL_TRIANGLE ignores stroke bits and fills with ink using
half-open pixel-centre coverage. Both windings are accepted; a zero-area fill
draws nothing, while TRIANGLE may still draw its edges.

### Text, bitmaps, patterns and tilemaps

DRAW_TEXT reads a string at the supplied address using the selected font's
glyph size. Every byte 0..127 selects a glyph, including byte zero; any byte
128..255 terminates, with 80h the conventional terminator. At most 255 glyph
bytes are consumed. Glyphs advance horizontally by their width, with no
newline interpretation. Set bits paint ink; unset bits paint paper only when
paper differs from the transparent key. Font and string data must be uploaded
byte-exact with UPLOAD_RAW or UNZX1_RAW.

BLIT draws the complete defined bitmap. CROP_BLIT draws a source rectangle at
its native size; STRETCH_BLIT maps a source rectangle to the supplied
destination rectangle. All use bit7 of their first coordinate high-bits byte
for transparency: 0 copies every sampled pixel, including the key; 1 preserves
the destination when the source pixel equals the key. Source coordinates are
bitmap-relative. The XY offset affects only destinations. Missing source
pixels outside the bitmap are skipped, not wrapped into unrelated resources.
Zero source or destination extent draws nothing. Odd X and odd widths retain
single-pixel precision. Scaling uses `floor(relative_dest * source_extent /
dest_extent)` per axis; destination clipping does not restart that sampling
phase. CROP_BLIT's Hdest uses X/Y bits only; STRETCH_BLIT's Hdest also packs
destination width/height. Bit6 of Hsource and bits7..6 of Hdest are ignored.

PATTERN_FILL repeats one 16×8 pattern over a rectangle. PATTERN_BUCKET_FILL
uses the same pattern within the seed-colour-connected region. Ink and paper
are not used. The pattern bank and pattern index are separate bytes; bank ID
uses six bits. The first coordinate byte's bit6 selects screen versus local
phase, and bit7 selects transparent versus opaque pattern pixels. A local
anchor is the effective, unclipped rectangle/seed position after XY offset;
a screen anchor is raw (0,0). Clipping never restarts the pattern. Pattern
bucket traversal tracks visited pixels separately, so a replacement pixel
equal to the seed colour does not stop the fill. Its finite span capacity is
17,381; exhaustion can leave a partial fill.

TILEMAP selects a map plus a source-cell rectangle. Its combined byte packs
map ID in bits7..3, destination X9..8 in bits2..1 and Y8 in bit0. Source column
and row are zero-based. Visible column/row bytes are literal counts; zero draws
nothing. A viewport extending beyond the map is reduced to the remaining
cells; a starting cell outside the map draws nothing. Each cell selects a
16×8 tile from the map's tileset. Tile pixels are copied, including the key;
host visibility still follows the global engine/transparent-colour rules.
Effective destination X rounds down to an even pixel; Y retains line precision.

## Mesh and camera data

A face's low nibble is its fill colour. Bits 4, 5, 6 enable edges 0→1, 1→2, 2→0; bit 7 makes the face double-sided. Faces are depth-sorted within each mesh draw; 2DPT order determines ordering between draw commands. WIREFRAME uses current ink for enabled edges; FILLED uses face colours; FWE draws fills and enabled ink-coloured edges.

All poses use signed 16-bit world coordinates and 10-bit angles. Angle high bits pack roll9..8 in bits 5..4, yaw9..8 in bits 3..2, pitch9..8 in bits 1..0; bits 7..6 are ignored. One turn is 1024 units. Object rotation order is roll → yaw → pitch. The global camera uses the corresponding inverse transform and is shared by both drawing tables.

SET_3D_PROJECTION accepts only nonzero FX/FY/near with far greater than near; invalid settings preserve the previous projection. Defaults: Enterprise FX=579, TVC FX=508, FY=300, near=96, far=2490. Descriptors, projection and camera pose latch with the accepted scene.

DEFINE_MESH takes `id, address[3], vertex_count_minus_1,
triangle_count_high, triangle_count_low`. Counts are therefore 1..256 vertices
and 1..512 triangles. The two-byte triangle value is **high part first** and
encodes count minus one; only high-byte bit0 is used. Each vertex is six bytes
of signed little-endian X/Y/Z; each triangle is three zero-based vertex indices
followed by its flags/colour byte. Total storage is `vertices*6 + triangles*4`.
The entire resource must fit before the end of resource RAM; it cannot wrap.

SET_3D_PROJECTION's eight bytes are `FXlo, FXhi, FYlo, FYhi, nearlo, nearhi,
farlo, farhi`. SET_CAMERA_POSE supplies the global camera's position and
orientation; its first six bytes are signed X/Y/Z and its last four are angle
high bits plus roll/yaw/pitch low bytes. DRAW_MESH_* uses the same pose encoding
after its slot byte, with the mesh ID last. Projection and camera are global,
shared by both layers; a mesh draw is a retained 2DPT command.

World axes are +X right, +Y up and +Z forward. Object rotations are ROLL about
Z, YAW about Y, then PITCH about X. Draw commands apply object pose, inverse
camera pose, clipping and perspective projection. A larger FX/FY enlarges the
projection on that axis. Invalid vertex indices are skipped.
SET_XY_OFFSET moves the projected mesh image in screen space without changing
its world pose; SET_CLIP_RECT clips its rendered pixels. WIREFRAME ignores paper.

## Logo and pointer

SHOW_BUILTIN_LOGO's XY high byte packs X9..8 in bits 2..1 and Y8 in bit 0. Bit 7 selects the 103×40 small logo; otherwise the logo is 206×79. The colour byte has main in its high nibble, accent in its low nibble. TVC uses fixed main/accent colours 15/3. The logo is independent of the 2DPT clip.

All pointers are 24×12 pixels. Built-in pointer colours use outer in the high
nibble and inner in the low nibble. Custom images retain their own palette
indices. A custom definition pending behind an upload/resource job completes
when it is safe to read those bytes; the latest pending valid definition wins.

Pointer position is its hotspot, with the same high-bits packing as SHOW_BUILTIN_LOGO. Movement while hidden is ignored. Coordinates clamp to Enterprise X=88..811, Y=18..303 or TVC X=79..686, Y=41..296; the image is clipped to those bounds. Pointer state is frame-latched, uses the transparent key, ignores the 2DPT clip and is composited above the logo but below the optional overrun marker.

### Pointer command details

`92h SET_POINTER_MODE` always takes two bytes, including when hiding the
pointer. Mode 0 hides; 1..14 selects a built-in shape; 15 selects the custom
image. Modes 16..255 are ignored completely, preserving mode and colours.
Selecting custom mode without a valid custom definition draws nothing.

`93h SET_POINTER_POS` takes `Hxy, Xlow, Ylow`, where Hxy bits2..1 are X9..8
and bit0 is Y8. Other bits are ignored. `94h SET_POINTER_REL` takes two signed
8-bit deltas, range −128..127. It adds them to the latest pending hotspot
position, even before that position has been displayed. An absolute command
replaces that pending position. Both commands are ignored while hidden.
The pointer initially appears at (450,161) on Enterprise or (383,169) on TVC;
hiding and re-enabling preserves its last position.

`95h DEFINE_USER_POINTER` takes `address[3], hotspot X, hotspot Y`. Hotspots
must lie within X=0..23 and Y=0..11. Invalid values preserve the existing
definition or pending valid request. A valid request copies 144 bytes when
resource access is safe; it does not enable the pointer. Re-uploading the
original resource later has no effect until the pointer is defined again.
Rows contain twelve packed bytes, low nibble first. Custom colours are pixel
indices; the SET_POINTER_MODE colour byte does not recolour this image.

| Mode | Built-in shape | Hotspot X,Y |
|---:|---|---|
| 1 | Top-left arrow | 0,0 |
| 2 | Top-right arrow | 23,0 |
| 3 | Hourglass | 11,5 |
| 4 | I-beam | 11,5 |
| 5 | Pointing hand | 8,0 |
| 6 | Grabbing hand | 12,5 |
| 7 | Crosshair | 11,5 |
| 8 | Horizontal resize | 11,5 |
| 9 | Vertical resize | 11,5 |
| 10 | NW–SE resize | 11,5 |
| 11 | NE–SW resize | 12,5 |
| 12 | Move symbol | 11,5 |
| 13 | Forbidden | 11,5 |
| 14 | Pencil | 2,11 |

Built-in shapes use transparent background plus outer/detail and inner/fill
roles. A role assigned to the global transparent index is also skipped.
Position denotes the hotspot; the bitmap's top-left is position minus hotspot.
The pointer cache, shape, colours and position publish with an accepted scene.

## Reserved commands and host integration boundary

`59h..5Ah`, `89h..8Dh`, `96h..BFh` and `7Dh/FDh` are unassigned.
`7Eh/FEh` is a reserved, unimplemented extension prefix. They have no resource
or drawing effect. `D6h..DFh` are unavailable demo selectors and stop demo
updates as described above. Do not invent parameter lengths for reserved
commands. Strict input sessions still ignore ordinary command-port writes.

The emulator implements these guest-visible operations as a behavioural model.
It returns palette indices, not final RGB values, and does not simulate the
hardware's instruction or electrical timing. Integrators must connect F8h/F9h,
HSYNC, VSYNC, raster IRQ, reset and overlay composition; those interfaces and
the timing boundary are documented in [INTEGRATION.md](INTEGRATION.md) and
[COMPATIBILITY.md](COMPATIBILITY.md).
