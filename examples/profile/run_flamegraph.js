const assert = require("assert");
const path = require("path");

const npxBin = process.env.PATH.split(path.delimiter).find(
  (entry) =>
    path.basename(entry) === ".bin" &&
    path.basename(path.dirname(entry)) === "node_modules",
);
assert(npxBin, "gb-flamegraph package directory is missing from PATH");

require(path.resolve(
  npxBin,
  "..",
  "gb-flamegraph",
  "src",
  "gb-flamegraph.js",
));
