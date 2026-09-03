#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const TILE_BYTES = 16;
const DITHER_SIZE = 64;
const TRANSPARENT_PIXELS = 16;
const OFFSET_STEP = 53; /* 6*8 + 5; coprime with 64 */
const CYCLE_UPDATES = DITHER_SIZE * DITHER_SIZE;
const COMMAND_TABLE_SIZE = DITHER_SIZE * 2;
const VALUES_PER_LINE = 16;

/* Inverted 8x8 void-and-cluster ranks generated with sigma 1.7, seed 184. */
const positions = [
  1, 36, 32, 60, 21, 63, 25, 51, 14, 47, 10, 28, 49, 61, 16, 45, 19, 41,
  15, 3, 38, 7, 34, 59, 30, 55, 18, 43, 13, 57, 24, 53, 20, 0, 39, 35, 5, 9,
  50, 46, 12, 23, 48, 26, 52, 22, 2, 54, 33, 29, 56, 17, 4, 44, 40, 6, 27,
  58, 31, 62, 11, 37, 8, 42,
];

function readTiles(sourcePath) {
  const source = fs.readFileSync(sourcePath, "utf8");
  const match = source.match(
    /const uint8_t tiles_tiles\[\d+\] = \{([\s\S]*?)\};/,
  );
  assert(match, "tiles_tiles data was not found");

  const bytes = Array.from(match[1].matchAll(/0x([0-9a-fA-F]{2})/g), (item) =>
    Number.parseInt(item[1], 16),
  );
  assert(bytes.length % TILE_BYTES === 0, "tile data is not 16-byte aligned");
  return bytes;
}

function bitMask(position) {
  return 0x80 >> (position & 7);
}

function rowOffset(position) {
  return (position >> 2) & 0x0e;
}

function buildTable(tile, tileAddressLow) {
  const table = [];

  for (let position = 0; position < DITHER_SIZE; position++) {
    const mask = bitMask(position);
    const offset = rowOffset(position);
    let encodedAddress = tileAddressLow + offset;

    if (tile[offset] & mask) encodedAddress |= 0x01;
    if (tile[offset + 1] & mask) encodedAddress |= 0x80;

    table.push(encodedAddress, mask);
  }

  assert.strictEqual(table.length, COMMAND_TABLE_SIZE);
  verifyTable(table, tile, tileAddressLow);
  return table;
}

function clearPixel(tile, address, mask, tileAddressLow) {
  const offset = address - tileAddressLow;
  tile[offset] &= ~mask;
  tile[offset + 1] &= ~mask;
}

function restorePixel(tile, command, tileAddressLow) {
  const restoreOffset = (command[0] & 0x7e) - tileAddressLow;
  if (command[0] & 0x01) tile[restoreOffset] |= command[1];
  if (command[0] & 0x80) tile[restoreOffset + 1] |= command[1];
}

function commandAt(table, position) {
  return table.slice(position * 2, position * 2 + 2);
}

function verifyTable(table, sourceTile, tileAddressLow) {
  const tile = sourceTile.slice();

  for (let rank = 0; rank < TRANSPARENT_PIXELS; rank++) {
    const command = commandAt(table, positions[rank]);
    clearPixel(tile, command[0] & 0x7e, command[1], tileAddressLow);
  }
  const initialTile = tile.slice();
  const transparentPositions = positions.slice(0, TRANSPARENT_PIXELS);

  let phase = 0;
  let clearOffset = 0;
  let restoreOffset = 0;
  for (let update = 0; update < CYCLE_UPDATES; update++) {
    const clearRank =
      (phase + TRANSPARENT_PIXELS) & (DITHER_SIZE - 1);
    const clearPosition =
      (positions[clearRank] + clearOffset) & (DITHER_SIZE - 1);
    const restorePosition =
      (positions[phase] + restoreOffset) & (DITHER_SIZE - 1);
    const clearCommand = commandAt(table, clearPosition);
    const restoreCommand = commandAt(table, restorePosition);

    assert.strictEqual(transparentPositions.shift(), restorePosition);
    assert(!transparentPositions.includes(clearPosition));
    transparentPositions.push(clearPosition);

    clearPixel(
      tile,
      clearCommand[0] & 0x7e,
      clearCommand[1],
      tileAddressLow,
    );
    restorePixel(tile, restoreCommand, tileAddressLow);

    if (clearRank === DITHER_SIZE - 1)
      clearOffset = (clearOffset + OFFSET_STEP) & (DITHER_SIZE - 1);
    if (phase === DITHER_SIZE - 1)
      restoreOffset = (restoreOffset + OFFSET_STEP) & (DITHER_SIZE - 1);
    phase = (phase + 1) & (DITHER_SIZE - 1);

    for (let index = 0; index < TILE_BYTES; index++) {
      assert.strictEqual(
        tile[index] & ~sourceTile[index],
        0,
        "dither must not create pixels outside the source tile",
      );
    }
  }

  assert.strictEqual(phase, 0);
  assert.strictEqual(clearOffset, 0);
  assert.strictEqual(restoreOffset, 0);
  assert.deepStrictEqual(
    tile,
    initialTile,
    "offset dither cycle must restore all pixels",
  );
}

function formatBytes(values) {
  const lines = [];
  for (let index = 0; index < values.length; index += VALUES_PER_LINE) {
    const line = values
      .slice(index, index + VALUES_PER_LINE)
      .map((value) => `0x${value.toString(16).padStart(2, "0")}`)
      .join(", ");
    lines.push(`    .db ${line}`);
  }
  return lines.join("\n");
}

const outputPath = process.argv[2];
const tilesPath = process.argv[3];
assert(
  outputPath && tilesPath,
  "usage: node generate_ghost_dither.js <output.{h,s}> <tiles.c>",
);

const tiles = readTiles(tilesPath);
const tileCount = tiles.length / TILE_BYTES;
const blackTileIndex = tileCount - 2;
const whiteTileIndex = tileCount - 1;
const blackTile = tiles.slice(
  blackTileIndex * TILE_BYTES,
  (blackTileIndex + 1) * TILE_BYTES,
);
const whiteTile = tiles.slice(
  whiteTileIndex * TILE_BYTES,
  (whiteTileIndex + 1) * TILE_BYTES,
);
const blackAddressLow = (blackTileIndex * TILE_BYTES) & 0xff;
const whiteAddressLow = (whiteTileIndex * TILE_BYTES) & 0xff;

assert(
  (blackTileIndex * TILE_BYTES) >> 8 ===
    (whiteTileIndex * TILE_BYTES) >> 8,
  "ghost tiles must share one VRAM page",
);

const headerOutput = `/* Generated by tools/generate_ghost_dither.js. */
#ifndef GHOST_DITHER_H
#define GHOST_DITHER_H

#define GHOST_DITHER_SIZE ${DITHER_SIZE}u
#define GHOST_DITHER_TRANSPARENT_PIXELS ${TRANSPARENT_PIXELS}u
#define GHOST_DITHER_OFFSET_STEP ${OFFSET_STEP}u
#define GHOST_DITHER_BLACK_TILE_INDEX ${blackTileIndex}u
#define GHOST_DITHER_WHITE_TILE_INDEX ${whiteTileIndex}u
#define GHOST_DITHER_TILE_VRAM_PAGE 0x${(
  0x80 + ((blackTileIndex * TILE_BYTES) >> 8)
)
  .toString(16)
  .padStart(2, "0")}u
#define GHOST_DITHER_POSITION_TABLE_PAGE 0x06u
#define GHOST_DITHER_BLACK_COMMAND_TABLE_PAGE 0x06u
#define GHOST_DITHER_WHITE_COMMAND_TABLE_PAGE 0x07u
#define GHOST_DITHER_COMMAND_TABLE_OFFSET 0x80u

#endif /* GHOST_DITHER_H */
`;

const asmOutput = `; Generated by tools/generate_ghost_dither.js.
.module ghost_dither

.area _GHOST_DITHER_POSITION

_ghost_dither_positions::
${formatBytes(positions)}

.area _GHOST_DITHER_BLACK
_ghost_dither_black::
${formatBytes(buildTable(blackTile, blackAddressLow))}

.area _GHOST_DITHER_WHITE
_ghost_dither_white::
${formatBytes(buildTable(whiteTile, whiteAddressLow))}
`;

const extension = path.extname(outputPath);
assert(extension === ".h" || extension === ".s", "output must be .h or .s");
fs.writeFileSync(outputPath, extension === ".h" ? headerOutput : asmOutput);
