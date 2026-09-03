#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");

const CLOCKS_PER_M_CYCLE = 4;
const VBLANK_M_CYCLES = 1140;
const MIN_NEXT_TIMER_GAP = 32;

const tracePath = process.argv[2];
assert(tracePath, "usage: node analyze_vblank.js <speedscope.json>");

const trace = JSON.parse(fs.readFileSync(tracePath, "utf8"));
const frames = trace.shared.frames.map((frame) => frame.name);
const events = trace.profiles[0].events;

function closeEvents(name) {
  const frame = frames.indexOf(name);
  assert.notStrictEqual(frame, -1, `missing profiler symbol: ${name}`);
  return events.filter(
    (event) =>
      event.type === "C" && event.frame === frame && event.at > event.start,
  );
}

function mCycles(clocks) {
  assert.strictEqual(clocks % CLOCKS_PER_M_CYCLE, 0);
  return clocks / CLOCKS_PER_M_CYCLE;
}

function stats(values) {
  assert(values.length > 0);
  const sorted = [...values].sort((left, right) => left - right);
  return {
    mean: values.reduce((sum, value) => sum + value, 0) / values.length,
    max: sorted[sorted.length - 1],
  };
}

const vblanks = closeEvents("__standard_VBL_handler");
const handoffs = closeEvents("gameplay_vbl_handoff_timer");
const timers = closeEvents("[INTERRUPT] TIM");
const trackers = closeEvents("_cursor_vbl_track");

assert.strictEqual(handoffs.length, vblanks.length);
assert.strictEqual(trackers.length, vblanks.length);

const criticalPrefix = [];
const timerCompletion = [];
const handoffDuration = [];
const trackingDuration = [];
const fullHandler = [];
const nextTimerGap = [];

for (const handoff of handoffs) {
  const vblank = vblanks.find(
    (candidate) =>
      candidate.start <= handoff.start && candidate.at >= handoff.at,
  );
  assert(vblank, "timer handoff must execute inside VBlank");

  const nestedTimers = timers.filter(
    (timer) => timer.start >= handoff.start && timer.at <= handoff.at,
  );
  assert.strictEqual(
    nestedTimers.length,
    1,
    "each VBlank handoff must service exactly one timer interrupt",
  );
  const timer = nestedTimers[0];

  const matchingTrackers = trackers.filter(
    (tracker) => tracker.start >= handoff.at && tracker.at <= vblank.at,
  );
  assert.strictEqual(
    matchingTrackers.length,
    1,
    "each VBlank must run tracking once after the timer handoff",
  );
  const tracker = matchingTrackers[0];

  assert(timer.start >= handoff.start);
  assert(timer.at <= handoff.at);
  assert(tracker.start >= handoff.at);

  criticalPrefix.push(mCycles(handoff.start - vblank.start));
  timerCompletion.push(mCycles(timer.at - vblank.start));
  handoffDuration.push(mCycles(handoff.at - handoff.start));
  trackingDuration.push(mCycles(tracker.at - tracker.start));
  fullHandler.push(mCycles(vblank.at - vblank.start));

  const nextTimer = timers.find((candidate) => candidate.start >= vblank.at);
  if (nextTimer)
    nextTimerGap.push(mCycles(nextTimer.start - vblank.at));
}

const criticalStats = stats(criticalPrefix);
const timerStats = stats(timerCompletion);
const handoffStats = stats(handoffDuration);
const trackingStats = stats(trackingDuration);
const fullStats = stats(fullHandler);
assert(nextTimerGap.length > 0, "trace must include a timer after VBlank");
const minimumNextTimerGap = Math.min(...nextTimerGap);

assert(
  criticalStats.max <= VBLANK_M_CYCLES,
  `OAM/VRAM work exceeded VBlank by ${
    criticalStats.max - VBLANK_M_CYCLES
  } M-cycles`,
);
assert(
  timerStats.max <= VBLANK_M_CYCLES,
  `dummy timer completed after VBlank by ${
    timerStats.max - VBLANK_M_CYCLES
  } M-cycles`,
);
assert(
  minimumNextTimerGap >= MIN_NEXT_TIMER_GAP,
  `late VBlank tail left only ${minimumNextTimerGap} M-cycles before the next timer`,
);

function formatStats(label, values) {
  console.log(
    `${label}: mean ${values.mean.toFixed(1)}, max ${values.max} M-cycles`,
  );
}

console.log(`VBlank safety verified across ${vblanks.length} frames.`);
formatStats("LCD-critical prefix", criticalStats);
formatStats("Dummy timer completion", timerStats);
formatStats("Timer handoff", handoffStats);
formatStats("WRAM-only cursor tracking", trackingStats);
formatStats("Full VBlank handler", fullStats);
console.log(
  `Next timer gap: minimum ${minimumNextTimerGap} M-cycles ` +
    `(required ${MIN_NEXT_TIMER_GAP})`,
);
