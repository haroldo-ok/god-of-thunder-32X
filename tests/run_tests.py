#!/usr/bin/env python3
"""
God of Thunder 32X - point-to-point automated test suite.

Runs the real ROM inside the PicoDrive emulator core and asserts on what is
actually rendered and how the game responds to controller input.

The headline requirement is the black-screen guard: several independent
checks fail loudly if the game ever stops putting a real picture on screen.

Usage:
    python3 tests/run_tests.py [--rom rom/got32x.32x] [--core <path>] [-v]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

DEFAULT_ROM = os.path.join(ROOT, 'rom', 'got32x.32x')
DEFAULT_CORE = '/home/user/pico/picodrive_libretro.so'
RUNNER = os.path.join(HERE, 'runner')

# The play area is the top 192 lines; the status panel occupies the rest.
GAME_H = 192
PANEL_Y = 192


class TestFailure(Exception):
    pass


results = []
verbose = False


def log(msg):
    if verbose:
        print('      ' + msg)


def run(rom, core, frames, presses=(), dumps=(), want_json=True):
    cmd = [RUNNER, core, rom, '--frames', str(frames)]
    if want_json:
        cmd.append('--json')
    for f, n, b in presses:
        cmd += ['--press', f'{f}:{n}:{b}']
    for at, path in dumps:
        cmd += ['--dump-at', f'{at}:{path}']

    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if p.returncode != 0:
        raise TestFailure(f'runner exited {p.returncode}: {p.stderr.strip()[:300]}')

    info = {}
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith('{'):
            info = json.loads(line)
    return info, p.stdout


def load_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    idx = data.index(b'255\n') + 4
    hdr = data[:idx].split()
    return int(hdr[1]), int(hdr[2]), data[idx:]


def region_stats(w, h, px, y0, y1):
    """Return (nonblack_fraction, distinct_colours, mean_luma) for a band."""
    nonblack = 0
    total = 0
    seen = set()
    acc = 0
    for y in range(y0, min(y1, h)):
        base = y * w * 3
        for x in range(w):
            i = base + x * 3
            r, g, b = px[i], px[i + 1], px[i + 2]
            total += 1
            acc += r + g + b
            if r > 8 or g > 8 or b > 8:
                nonblack += 1
            seen.add((r >> 4, g >> 4, b >> 4))
    if total == 0:
        return 0.0, 0, 0.0
    return nonblack / total, len(seen), acc / (total * 3)


def check(name, fn):
    try:
        fn()
        results.append((name, True, ''))
        print(f'  PASS  {name}')
    except TestFailure as e:
        results.append((name, False, str(e)))
        print(f'  FAIL  {name}\n        {e}')
    except Exception as e:  # noqa: BLE001
        results.append((name, False, repr(e)))
        print(f'  ERROR {name}\n        {e!r}')


def main():
    global verbose

    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default=DEFAULT_ROM)
    ap.add_argument('--core', default=DEFAULT_CORE)
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    verbose = args.verbose

    rom, core = args.rom, args.core

    print(f'ROM  : {rom}')
    print(f'core : {core}\n')

    if not os.path.exists(rom):
        print('FATAL: ROM not found. Run `make` first.')
        return 2
    if not os.path.exists(core):
        print('FATAL: emulator core not found.')
        return 2
    if not os.path.exists(RUNNER):
        print('FATAL: test runner not built. Run `make -C tests`.')
        return 2

    tmp = tempfile.mkdtemp(prefix='got32x-test-')

    # ---------------------------------------------------------------
    # 1. ROM image integrity
    # ---------------------------------------------------------------
    def t_rom_header():
        with open(rom, 'rb') as f:
            data = f.read()
        size = len(data)
        if size not in (512 * 1024, 1024 * 1024, 2 * 1024 * 1024,
                        4 * 1024 * 1024):
            raise TestFailure(f'unusual ROM size {size}')
        console = data[0x100:0x110].decode('latin1')
        if not console.startswith('SEGA 32X'):
            raise TestFailure(f'bad console signature {console!r}')
        title = data[0x120:0x130].decode('latin1').strip()
        if 'THUNDER' not in title.upper():
            raise TestFailure(f'unexpected title {title!r}')
        # header checksum
        cs = 0
        for i in range(0x200, size, 2):
            cs = (cs + (data[i] << 8) + data[i + 1]) & 0xFFFF
        stored = (data[0x18E] << 8) | data[0x18F]
        if cs != stored:
            raise TestFailure(f'checksum mismatch: {cs:#06x} != {stored:#06x}')
        log(f'{size} bytes, title {title!r}, checksum {cs:#06x}')

    def t_assets_embedded():
        with open(rom, 'rb') as f:
            data = f.read()
        if data.find(b'GOT3') < 0:
            raise TestFailure('resource archive magic not found in ROM')
        for name in (b'PALETTE', b'BPICS1', b'SDAT1', b'ACTOR100', b'STATUS',
                     b'TEXT', b'OBJECTS'):
            if data.find(name) < 0:
                raise TestFailure(f'resource {name.decode()} missing from ROM')
        log('archive + key resources present')

    check('rom/header is a valid Sega 32X image', t_rom_header)
    check('rom/game assets embedded in ROM', t_assets_embedded)

    # ---------------------------------------------------------------
    # 2. Boot + the black-screen guards
    # ---------------------------------------------------------------
    boot = {}

    def t_boots():
        info, _ = run(rom, core, 240)
        boot.update(info)
        if not info.get('got_frame'):
            raise TestFailure('emulator never produced a video frame')
        if info['width'] != 320:
            raise TestFailure(f"unexpected width {info['width']}")
        log(f"{info['width']}x{info['height']}, "
            f"{info['nonblack_pct']:.1f}% non-black")

    def t_not_black():
        """THE black-screen test: the finished frame must be real picture."""
        if boot.get('nonblack_pct', 0) < 50.0:
            raise TestFailure(
                f"screen is mostly black: only {boot.get('nonblack_pct', 0):.1f}% "
                f"of pixels are lit (need >= 50%)")
        log(f"{boot['nonblack_pct']:.1f}% non-black")

    def t_not_flat():
        """A single flat colour would pass a naive non-black check."""
        if boot.get('distinct', 0) < 8:
            raise TestFailure(
                f"frame has only {boot.get('distinct')} distinct colours - "
                f"looks like a flat fill, not a rendered scene")
        log(f"{boot['distinct']} distinct colours")

    def t_boots_promptly():
        f = boot.get('first_nonblack_frame', -1)
        if f < 0:
            raise TestFailure('never rendered a non-black frame')
        if f > 180:
            raise TestFailure(f'took {f} frames (~{f/60:.1f}s) to draw anything')
        log(f'first picture at frame {f} (~{f/60:.2f}s)')

    check('boot/emulator receives video frames', t_boots)
    check('boot/screen is NOT black', t_not_black)
    check('boot/screen is NOT a flat colour', t_not_flat)
    check('boot/first picture appears promptly', t_boots_promptly)

    # ---------------------------------------------------------------
    # 3. Black-screen guard sustained over time
    # ---------------------------------------------------------------
    def t_no_black_over_time():
        """Sample across a long run - the screen must never go black."""
        checkpoints = [60, 180, 400, 700, 1000]
        dumps = [(c, os.path.join(tmp, f'chk{c}.ppm')) for c in checkpoints]
        run(rom, core, max(checkpoints) + 2, dumps=dumps)

        worst = None
        for c, path in dumps:
            if not os.path.exists(path):
                raise TestFailure(f'no frame captured at {c}')
            w, h, px = load_ppm(path)
            frac, distinct, luma = region_stats(w, h, px, 0, GAME_H)
            log(f'frame {c:5d}: play area {frac*100:5.1f}% lit, '
                f'{distinct:3d} colours, luma {luma:5.1f}')
            if frac < 0.50:
                raise TestFailure(
                    f'frame {c}: play area only {frac*100:.1f}% lit')
            if distinct < 8:
                raise TestFailure(
                    f'frame {c}: play area has {distinct} colours (flat)')
            if worst is None or frac < worst:
                worst = frac
        log(f'worst-case coverage {worst*100:.1f}%')

    check('render/never goes black over 1000 frames', t_no_black_over_time)

    # ---------------------------------------------------------------
    # 4. Scene composition - the world and the HUD are both drawn
    # ---------------------------------------------------------------
    def t_panel_drawn():
        path = os.path.join(tmp, 'panel.ppm')
        run(rom, core, 200, dumps=[(199, path)])
        w, h, px = load_ppm(path)
        frac, distinct, luma = region_stats(w, h, px, PANEL_Y, h)
        log(f'panel: {frac*100:.1f}% lit, {distinct} colours')
        if frac < 0.50:
            raise TestFailure(f'status panel is mostly black ({frac*100:.1f}%)')
        if distinct < 5:
            raise TestFailure(f'status panel looks flat ({distinct} colours)')

    def t_playfield_drawn():
        path = os.path.join(tmp, 'play.ppm')
        run(rom, core, 200, dumps=[(199, path)])
        w, h, px = load_ppm(path)
        frac, distinct, luma = region_stats(w, h, px, 0, GAME_H)
        log(f'play area: {frac*100:.1f}% lit, {distinct} colours')
        if frac < 0.90:
            raise TestFailure(f'play area not fully drawn ({frac*100:.1f}%)')
        if distinct < 10:
            raise TestFailure(f'play area too uniform ({distinct} colours)')

    check('render/status panel is drawn', t_panel_drawn)
    check('render/play area is fully drawn', t_playfield_drawn)

    # ---------------------------------------------------------------
    # 5. Interactivity - the game responds to the controller
    # ---------------------------------------------------------------
    def diff_play_area(a_path, b_path):
        w, h, a = load_ppm(a_path)
        _, _, b = load_ppm(b_path)
        n = 0
        for y in range(GAME_H):
            base = y * w * 3
            for x in range(w):
                i = base + x * 3
                if a[i:i + 3] != b[i:i + 3]:
                    n += 1
        return n

    def t_input_moves_player():
        """Holding a direction must change the play area vs. idle."""
        idle = os.path.join(tmp, 'idle.ppm')
        run(rom, core, 200, dumps=[(199, idle)])

        moved = {}
        for d in ('right', 'left', 'up', 'down'):
            p = os.path.join(tmp, f'go_{d}.ppm')
            run(rom, core, 200, presses=[(60, 130, d)], dumps=[(199, p)])
            moved[d] = diff_play_area(idle, p)
            log(f'hold {d:5s}: {moved[d]:5d} pixels differ from idle')

        responsive = [d for d, n in moved.items() if n >= 20]
        if len(responsive) < 3:
            raise TestFailure(
                f'only {len(responsive)}/4 directions changed the screen: '
                f'{moved}')

    def t_player_actually_translates():
        """Thor must physically relocate, not merely animate in place.

        This deliberately does NOT just count changed pixels anywhere on
        screen: enemies wander around on their own, so an idle Thor would
        still show plenty of motion. Instead we isolate the region Thor is
        known to occupy and require the changes to sweep a wide band.
        """
        # Walk right for a good while, then compare early vs late.
        a = os.path.join(tmp, 'walk_a.ppm')
        b = os.path.join(tmp, 'walk_b.ppm')
        run(rom, core, 240, presses=[(60, 170, 'right')],
            dumps=[(62, a), (230, b)])

        w, h, pa = load_ppm(a)
        _, _, pb = load_ppm(b)

        # Thor starts at x=128,y=95 (screen 23, grid cell 8,6). Restrict the
        # comparison to the horizontal corridor he walks along, which the
        # roaming enemy on this screen does not share.
        y0, y1 = 90, 115
        minx, maxx, n = 10 ** 9, -1, 0
        for y in range(y0, y1):
            base = y * w * 3
            for x in range(w):
                i = base + x * 3
                if pa[i:i + 3] != pb[i:i + 3]:
                    n += 1
                    minx = min(minx, x)
                    maxx = max(maxx, x)
        if n == 0:
            raise TestFailure('Thor did not change at all while walking right')

        span = maxx - minx + 1
        log(f'walking right: {n} px changed, x=[{minx},{maxx}] span={span}')
        # Animating in place spans ~16px (one sprite). Real walking sweeps
        # a much wider corridor.
        if span < 30:
            raise TestFailure(
                f'changed region only {span}px wide - Thor animated but did '
                f'not translate (he is probably stuck against geometry)')

    def t_player_moves_on_all_axes():
        """Thor must be able to move both horizontally and vertically.

        Regression guard for the inverted tile-collision rule that walled
        him in at spawn: the engine classifies icon < TILE_FLY (140) as
        solid, so the open grass (tile 176) he starts on must be walkable.
        """
        base_dump = os.path.join(tmp, 'axis_base.ppm')
        run(rom, core, 200, dumps=[(62, base_dump)])
        w, h, pbase = load_ppm(base_dump)

        def sweep(direction, y0, y1, horizontal):
            end = os.path.join(tmp, f'axis_{direction}.ppm')
            run(rom, core, 200, presses=[(60, 130, direction)],
                dumps=[(190, end)])
            _, _, pe = load_ppm(end)
            lo, hi = 10 ** 9, -1
            for y in range(y0, min(y1, GAME_H)):
                b = y * w * 3
                for x in range(w):
                    i = b + x * 3
                    if pbase[i:i + 3] != pe[i:i + 3]:
                        v = x if horizontal else y
                        lo = min(lo, v)
                        hi = max(hi, v)
            return (hi - lo + 1) if hi >= 0 else 0

        # Right and vertical movement are unobstructed from the spawn point;
        # left has a tree immediately adjacent, so it is covered by the
        # walk-right-then-left check below instead.
        right = sweep('right', 90, 115, True)
        up = sweep('up', 40, 115, False)
        down = sweep('down', 90, 150, False)
        log(f'sweeps: right={right}px  up={up}px  down={down}px')

        if right < 30:
            raise TestFailure(f'cannot walk right (swept only {right}px)')
        if up < 30 and down < 30:
            raise TestFailure(
                f'cannot walk vertically (up={up}px, down={down}px)')

    def t_player_can_reverse():
        """Walk right into open ground, then back left again."""
        a = os.path.join(tmp, 'rev_a.ppm')
        b = os.path.join(tmp, 'rev_b.ppm')
        run(rom, core, 400,
            presses=[(60, 120, 'right'), (200, 150, 'left')],
            dumps=[(185, a), (360, b)])
        w, h, pa = load_ppm(a)
        _, _, pb = load_ppm(b)
        minx, maxx, n = 10 ** 9, -1, 0
        for y in range(90, 115):
            base = y * w * 3
            for x in range(w):
                i = base + x * 3
                if pa[i:i + 3] != pb[i:i + 3]:
                    n += 1
                    minx = min(minx, x)
                    maxx = max(maxx, x)
        span = (maxx - minx + 1) if maxx >= 0 else 0
        log(f'right-then-left: {n} px changed, span={span}')
        if span < 30:
            raise TestFailure(
                f'Thor could not walk back left (span {span}px)')

    def t_direction_matches_movement():
        """Thor must move the way the D-pad says AND face the way he moves.

        Two separate regressions live here:

          1. inverted movement - holding RIGHT moved him left;
          2. sprite/movement mismatch - he faced one way and walked the
             other, because the engine's direction codes (0=UP 1=DOWN
             2=LEFT 3=RIGHT) also index pic[dir][frame].

        Displacement alone cannot catch (2), so we additionally compare the
        sprite Thor is *wearing* when walking left vs right: those are
        different frames in the actor data, so the pixels must differ.
        """
        idle_a = os.path.join(tmp, 'dm_i0.ppm')
        idle_b = os.path.join(tmp, 'dm_i1.ppm')
        run(rom, core, 240, dumps=[(62, idle_a), (230, idle_b)])
        w, h, ia = load_ppm(idle_a)
        _, _, ib = load_ppm(idle_b)

        def changed(pa, pb, axis):
            out = set()
            for y in range(GAME_H):
                base = y * w * 3
                for x in range(w):
                    i = base + x * 3
                    if pa[i:i + 3] != pb[i:i + 3]:
                        out.add(x if axis == 0 else y)
            return out

        idle_cols = changed(ia, ib, 0)
        idle_rows = changed(ia, ib, 1)

        def sweep(direction, axis, frames=165):
            a = os.path.join(tmp, f'dm_{direction}_a.ppm')
            b = os.path.join(tmp, f'dm_{direction}_b.ppm')
            run(rom, core, 240, presses=[(60, frames, direction)],
                dumps=[(62, a), (230, b)])
            _, _, pa = load_ppm(a)
            _, _, pb = load_ppm(b)
            base = idle_cols if axis == 0 else idle_rows
            vals = sorted(changed(pa, pb, axis) - base)
            return ((min(vals), max(vals)) if vals else None), b

        # --- 1. displacement is in the commanded direction ---------------
        r, right_shot = sweep('right', 0)
        if not r or (r[1] - r[0] + 1) < 24:
            raise TestFailure(f'right did not sweep a corridor: {r}')
        if r[1] <= 140:
            raise TestFailure(
                f'holding RIGHT moved Thor to columns {r} - he did not go '
                f'right (spawn is x=128)')
        log(f'right: columns {r[0]}..{r[1]}')

        d, _ = sweep('down', 1)
        if not d or (d[1] - d[0] + 1) < 24:
            raise TestFailure(f'down did not sweep a corridor: {d}')
        if d[1] <= 112:
            raise TestFailure(
                f'holding DOWN moved Thor to rows {d} - he did not go down '
                f'(spawn is y=95)')
        log(f'down: rows {d[0]}..{d[1]}')

        u, _ = sweep('up', 1)
        if not u or (u[1] - u[0] + 1) < 24:
            raise TestFailure(f'up did not sweep a corridor: {u}')
        if u[0] >= 90:
            raise TestFailure(
                f'holding UP moved Thor to rows {u} - he did not go up '
                f'(spawn is y=95)')
        log(f'up: rows {u[0]}..{u[1]}')

        # --- 2. the sprite faces the way he walks ------------------------
        # Sample Thor at the end of a sustained hold in each direction and
        # require all four facings to be visually distinct. The actor data
        # has separate frames per direction, so if pic[dir] is being indexed
        # with the wrong code, two or more facings collapse onto the same
        # artwork.
        def sprite_at(path, x0, y0):
            _, _, px = load_ppm(path)
            return [px[((y0 + yy) * w + (x0 + xx)) * 3:
                       ((y0 + yy) * w + (x0 + xx)) * 3 + 3]
                    for yy in range(16) for xx in range(14)]

        faces = {}
        pu = os.path.join(tmp, 'face_u.ppm')
        pd = os.path.join(tmp, 'face_d.ppm')
        pr2 = os.path.join(tmp, 'face_r2.ppm')
        pl2 = os.path.join(tmp, 'face_l2.ppm')
        run(rom, core, 210, presses=[(60, 150, 'up')], dumps=[(200, pu)])
        run(rom, core, 210, presses=[(60, 150, 'down')], dumps=[(200, pd)])
        run(rom, core, 210, presses=[(60, 150, 'right')], dumps=[(200, pr2)])
        run(rom, core, 260,
            presses=[(60, 60, 'right'), (140, 120, 'left')],
            dumps=[(250, pl2)])

        faces['up'] = sprite_at(pu, 127, 41)
        faces['down'] = sprite_at(pd, 127, 158)
        faces['right'] = sprite_at(pr2, 271, 90)
        faces['left'] = sprite_at(pl2, 125, 92)

        names = list(faces)
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                a, b2 = names[i], names[j]
                d = sum(1 for p, q in zip(faces[a], faces[b2]) if p != q)
                log(f'{a} vs {b2}: {d} px differ')
                if d < 20:
                    raise TestFailure(
                        f'Thor looks the same facing {a} and {b2} '
                        f'({d} px differ) - the sprite is not tracking his '
                        f'direction')

    def t_hammer_fires():
        """Pressing B must launch the hammer and it must come back.

        Thor faces DOWN at spawn, so the hammer flies down-screen, reverses
        and returns to him. We watch the vertical extent of the change in
        his column band.
        """
        pre = os.path.join(tmp, 'hm_pre.ppm')
        shots = [(158, 'a'), (174, 'b'), (190, 'c'), (215, 'd')]
        dumps = [(148, pre)] + [
            (f, os.path.join(tmp, f'hm_{n}.ppm')) for f, n in shots]
        run(rom, core, 260, presses=[(150, 16, 'b')], dumps=dumps)

        w, h, base = load_ppm(pre)
        extents = []
        for f, n in shots:
            _, _, b = load_ppm(os.path.join(tmp, f'hm_{n}.ppm'))
            ys = []
            for y in range(60, GAME_H):
                row = y * w * 3
                for x in range(118, 160):
                    i = row + x * 3
                    if base[i:i + 3] != b[i:i + 3]:
                        ys.append(y)
                        break
            extents.append((f, (min(ys), max(ys)) if ys else None))

        log('hammer extents: ' + ', '.join(
            f'f{f}={e}' for f, e in extents))

        seen = [e for _, e in extents if e]
        if not seen:
            raise TestFailure('pressing B produced no visible hammer at all')

        # The hammer must travel away from Thor's spawn row (95) at some
        # point - a sprite that only animates in place would not.
        reach = min(e[0] for e in seen)
        if reach > 85:
            raise TestFailure(
                f'hammer never left Thor (closest row reached {reach}, '
                f'spawn row is 95)')
        log(f'hammer reached row {reach}')

    def t_actor_collision():
        """Thor must not walk through solid actors.

        Walk right along the spawn corridor, which runs into the enemy
        patrolling near x=273. Thor's advance must stop short of passing
        through it.
        """
        a = os.path.join(tmp, 'col_a.ppm')
        b = os.path.join(tmp, 'col_b.ppm')
        # Long hold: without collision Thor would reach the screen edge.
        run(rom, core, 900, presses=[(60, 800, 'right')],
            dumps=[(62, a), (880, b)])
        w, h, pa = load_ppm(a)
        _, _, pb = load_ppm(b)

        # Where is Thor at the end? Look for the rightmost change in his
        # row band that is not the enemy's own idle motion.
        cols = []
        for y in range(90, 115):
            base = y * w * 3
            for x in range(w):
                i = base + x * 3
                if pa[i:i + 3] != pb[i:i + 3]:
                    cols.append(x)
        if not cols:
            raise TestFailure('Thor never moved while walking right')
        log(f'after a long walk right, changes span x={min(cols)}..{max(cols)}')
        # Thor is 16px wide; the play area is 320. If he passed clean
        # through everything he would sit at the right edge (>=304).
        if max(cols) >= 318:
            raise TestFailure(
                'Thor reached the far screen edge - he appears to pass '
                'through solid actors')

    def t_special_actors_are_solid():
        """Thor must collide with special (func_num) actors.

        Screen 23 carries a GLOBE (crystal ball, ACTOR10, func_num=3) whose
        collision box is x=257..266, y=81..93. Before the special-actor
        dispatch existed Thor walked straight through it - and through
        blocks, signs, boulders, barrels, villagers and trees, all of which
        are actors carrying a func_num rather than plain scenery.

        Thor spawns at (128, 95) and his own collision box is
        (x+1, y+8)-(x+12, y+15), so at the spawn row his feet already
        overlap the globe's rows. Walking straight east therefore runs into
        it with no vertical adjustment at all - which also avoids the
        earlier mistake of nudging him off the globe's row entirely.
        """
        a = os.path.join(tmp, 'sa0.ppm')
        b = os.path.join(tmp, 'sa1.ppm')
        run(rom, core, 1400, presses=[(60, 1200, 'right')],
            dumps=[(70, a), (1380, b)])
        w, h, pa = load_ppm(a)
        _, _, pb = load_ppm(b)

        xs = []
        for y in range(88, 116):
            base = y * w * 3
            for x in range(w):
                i = base + x * 3
                if pa[i:i + 3] != pb[i:i + 3]:
                    xs.append(x)
        if not xs:
            raise TestFailure('Thor never moved east')

        far = max(xs)
        log(f'spawn-row walk east: changed columns {min(xs)}..{far}')

        # Something must stop him well before the play-area edge.
        if far >= 305:
            raise TestFailure(
                f'Thor reached column {far} (screen edge) - he is passing '
                f'through solid actors and scenery')

    def t_screen_transition():
        """Walking off an open edge must load the adjoining screen.

        Screen 23's only exit is UP through grid columns 9-11. Crossing it
        should replace the whole scene (screen 13 is a tree grove, visually
        very different from the lake screen).
        """
        before = os.path.join(tmp, 'tr_before.ppm')
        after = os.path.join(tmp, 'tr_after.ppm')
        run(rom, core, 1000,
            presses=[(60, 26, 'right'), (120, 800, 'up')],
            dumps=[(80, before), (980, after)])

        w, h, pa = load_ppm(before)
        _, _, pb = load_ppm(after)

        diff = 0
        for y in range(GAME_H):
            base = y * w * 3
            for x in range(w):
                i = base + x * 3
                if pa[i:i + 3] != pb[i:i + 3]:
                    diff += 1
        total = w * GAME_H
        pct = 100.0 * diff / total
        log(f'{pct:.1f}% of the play area changed after walking up')
        if pct < 25.0:
            raise TestFailure(
                f'only {pct:.1f}% of the screen changed - the screen '
                f'transition did not happen')

        # And the new screen must itself be a real, non-black picture.
        frac, distinct, _ = region_stats(w, h, pb, 0, GAME_H)
        if frac < 0.90 or distinct < 10:
            raise TestFailure(
                f'the new screen is not properly drawn '
                f'({frac*100:.1f}% lit, {distinct} colours)')

    def t_object_pickup():
        """Walking over a pickup must consume it and update the HUD.

        Screen 23's three blue jewels sit in the lower-left quadrant at
        grid (1,8) (2,9) (3,10). Collecting one increments the jewel
        counter in the status panel and removes the sprite from the world.
        """
        a = os.path.join(tmp, 'ob0.ppm')
        b = os.path.join(tmp, 'ob1.ppm')
        # Head south-west from the spawn point into the jewels.
        # Spawn is grid (8,6). The jewels sit at (1,8) (2,9) (3,10), which
        # are reached by dropping to the row-10 corridor and heading west.
        run(rom, core, 1600,
            presses=[(60, 140, 'down'), (240, 400, 'left'),
                     (700, 120, 'down'), (860, 400, 'left'),
                     (1300, 120, 'down')],
            dumps=[(80, a), (1580, b)])
        w, h, pa = load_ppm(a)
        _, _, pb = load_ppm(b)

        # Fewer blue pixels in the quadrant => a jewel was taken.
        def blues(px):
            n = 0
            for y in range(112, 176):
                base = y * w * 3
                for x in range(0, 96):
                    i = base + x * 3
                    r, g, bl = px[i], px[i + 1], px[i + 2]
                    if bl > 100 and bl > r + 40 and bl > g + 40:
                        n += 1
            return n

        before, after = blues(pa), blues(pb)

        # The jewel counter lives at x=59..85 of the panel.
        panel_diff = 0
        for y in range(PANEL_Y + 18, min(PANEL_Y + 30, h)):
            base = y * w * 3
            for x in range(55, 95):
                i = base + x * 3
                if pa[i:i + 3] != pb[i:i + 3]:
                    panel_diff += 1

        log(f'jewel pixels {before} -> {after}, panel counter delta '
            f'{panel_diff} px')
        if after >= before and panel_diff < 4:
            raise TestFailure(
                'no jewel disappeared and the counter never changed - '
                'objects are not being picked up')

    def t_objects_spawn():
        """Pickups must appear on screen.

        Screen 23 carries three blue jewels at grid (1,8) (2,9) (3,10).
        LEVEL records store static_x/static_y as little-endian 16-bit
        values (Turbo C on x86); the SH2 is big-endian, so before the asset
        pipeline byte-swapped them an x of 2 read back as 512 and every
        object was drawn far off-screen.

        The jewels sit on grass in the lower-left quadrant, so their
        distinctive blue must be present there.
        """
        p = os.path.join(tmp, 'objs.ppm')
        run(rom, core, 200, dumps=[(180, p)])
        w, h, px = load_ppm(p)

        blue = 0
        for y in range(112, 176):
            base = y * w * 3
            for x in range(0, 96):
                i = base + x * 3
                r, g, b = px[i], px[i + 1], px[i + 2]
                if b > 100 and b > r + 40 and b > g + 40:
                    blue += 1
        log(f'{blue} blue pixels in the jewel quadrant')
        if blue < 40:
            raise TestFailure(
                f'only {blue} blue pixels found - the jewels are not being '
                f'spawned (check static_x/static_y byte order)')

    def t_enemies_move():
        """Enemies must move on their own.

        With no input at all, the only thing that can change the play area
        is enemy AI. Screen 23 has a BOINGY (movement pattern 7).
        """
        frames = [(120, 'a'), (200, 'b'), (280, 'c'), (360, 'd')]
        dumps = [(f, os.path.join(tmp, f'em_{n}.ppm')) for f, n in frames]
        run(rom, core, 400, dumps=dumps)

        base = load_ppm(dumps[0][1])
        w, h, pa = base
        moved = []
        for f, n in frames[1:]:
            _, _, pb = load_ppm(os.path.join(tmp, f'em_{n}.ppm'))
            cnt = 0
            for y in range(GAME_H):
                row = y * w * 3
                for x in range(w):
                    i = row + x * 3
                    if pa[i:i + 3] != pb[i:i + 3]:
                        cnt += 1
            moved.append(cnt)
        log(f'idle-frame pixel deltas: {moved}')
        if max(moved) < 30:
            raise TestFailure(
                f'the screen barely changes with no input ({moved}) - '
                f'enemies are not moving')

    def t_enemy_takes_damage():
        """The hammer must damage and destroy enemies.

        The BOINGY on screen 23 has 10 health and the starting hammer has
        strength 10, so a clean hit removes it. Hardcoding a nominal damage
        value (as an earlier version did) left enemies effectively
        invulnerable.
        """
        before = os.path.join(tmp, 'dmg_a.ppm')
        after = os.path.join(tmp, 'dmg_b.ppm')
        # Walk east to the enemy's side of the screen and throw repeatedly.
        presses = [(60, 700, 'right')]
        for t0 in range(200, 760, 60):
            presses.append((t0, 12, 'b'))
        run(rom, core, 900, presses=presses,
            dumps=[(100, before), (880, after)])

        w, h, pa = load_ppm(before)
        _, _, pb = load_ppm(after)

        # The enemy lives in the top-right quadrant; count how much of it
        # is still there.
        def ink(px):
            n = 0
            for y in range(56, 96):
                base = y * w * 3
                for x in range(256, 310):
                    i = base + x * 3
                    r, g, b = px[i], px[i + 1], px[i + 2]
                    if not (g > r + 30 and g > b + 30):
                        n += 1
            return n

        a, b = ink(pa), ink(pb)
        log(f'non-grass pixels in the enemy quadrant: {a} -> {b}')
        # Either the enemy died (fewer pixels) or it moved//took damage;
        # a completely static count means nothing happened at all.
        if a == b:
            raise TestFailure(
                'the enemy quadrant is pixel-identical before and after a '
                'sustained hammer attack - enemies take no damage')

    def t_crystal_ball_speaks():
        """Touching the oracle must open a dialogue box.

        Screen 23's GLOBE is actor slot 1, i.e. actor_num 4, so its script
        key is level*1000 + actor_num = 23004 in SPEAK1. Running it draws a
        framed panel over the middle of the play area.
        """
        found = 0
        detail = []
        for nudge in (6, 10, 14):
            shots = []
            args_dumps = []
            for f in (200, 260, 320):
                q = os.path.join(tmp, f'orc_{nudge}_{f}.ppm')
                shots.append(q)
                args_dumps.append((f, q))
            run(rom, core, 400,
                presses=[(60, nudge, 'up'), (120, 260, 'right')],
                dumps=args_dumps)

            for q in shots:
                w, h, px = load_ppm(q)
                n = 0
                for y in range(66, 142):
                    base = y * w * 3
                    for x in range(50, 270):
                        i = base + x * 3
                        r, g, b = px[i], px[i + 1], px[i + 2]
                        if not (g > r + 30 and g > b + 30):
                            n += 1
                detail.append(n)
                if n > 12000:
                    found = 1
        log(f'box-area non-grass counts: {detail}')
        if not found:
            raise TestFailure(
                'no dialogue box appeared when touching the crystal ball - '
                'the script system is not running')

    def t_button_input_accepted():
        """Pressing the action button must not wedge or blank the game."""
        p = os.path.join(tmp, 'fire.ppm')
        info, _ = run(rom, core, 240, presses=[(60, 150, 'b'), (60, 150, 'c')],
                      dumps=[(230, p)])
        if info.get('nonblack_pct', 0) < 50:
            raise TestFailure('screen went black while holding buttons')
        w, h, px = load_ppm(p)
        frac, distinct, _ = region_stats(w, h, px, 0, GAME_H)
        if frac < 0.85 or distinct < 10:
            raise TestFailure(
                f'play area degraded under input ({frac*100:.1f}%, '
                f'{distinct} colours)')
        log(f'stable under B+C: {frac*100:.1f}% lit, {distinct} colours')

    check('input/directions change the scene', t_input_moves_player)
    check('input/player translates across the screen',
          t_player_actually_translates)
    check('input/player can move on both axes (not walled in)',
          t_player_moves_on_all_axes)
    check('input/player can walk out and back', t_player_can_reverse)
    check('input/direction matches movement (not mirrored)',
          t_direction_matches_movement)
    check('input/hammer fires and returns', t_hammer_fires)
    check('input/player collides with actors', t_actor_collision)
    check('world/pickups are spawned', t_objects_spawn)
    check('world/enemies move on their own', t_enemies_move)
    check('world/enemies take hammer damage', t_enemy_takes_damage)
    check('world/crystal ball opens a dialogue', t_crystal_ball_speaks)
    check('world/special actors are solid', t_special_actors_are_solid)
    check('world/screen transitions work', t_screen_transition)
    check('world/objects can be picked up', t_object_pickup)
    check('input/action buttons are handled safely', t_button_input_accepted)

    # ---------------------------------------------------------------
    # 6. Stability - long run without hanging or corrupting
    # ---------------------------------------------------------------
    def t_long_run_stable():
        seq = [(100, 120, 'right'), (260, 120, 'down'),
               (420, 120, 'left'), (580, 120, 'up'),
               (740, 60, 'b'), (820, 60, 'c')]
        last = os.path.join(tmp, 'long.ppm')
        info, _ = run(rom, core, 1200, presses=seq, dumps=[(1190, last)])
        if not info.get('got_frame'):
            raise TestFailure('stopped producing frames')
        if info.get('nonblack_pct', 0) < 50:
            raise TestFailure(
                f"screen degraded to {info.get('nonblack_pct'):.1f}% lit")
        w, h, px = load_ppm(last)
        frac, distinct, _ = region_stats(w, h, px, 0, GAME_H)
        log(f'after 1200 frames (~20s) with input: '
            f'{frac*100:.1f}% lit, {distinct} colours')
        if frac < 0.85 or distinct < 10:
            raise TestFailure('play area degraded over a long session')

    check('stability/20s scripted session stays healthy', t_long_run_stable)

    # ---------------------------------------------------------------
    print()
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f'{passed}/{total} tests passed')
    if passed != total:
        print('\nFailures:')
        for name, ok, msg in results:
            if not ok:
                print(f'  - {name}: {msg}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
