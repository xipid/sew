import { XylemEngine } from "Xylem/Xylem.hpp";

const db = new XylemEngine();
db.config.deviceSize = 10 * 1024 * 1024;
db.config.blockSize = 4096;
db.maxCache = 5 * 1024 * 1024;

const formatted = db.format();
console.log("format succeeded:", formatted);

const mounted = db.mount();
console.log("mount succeeded:", mounted);

const helloResult = db.tee("/hello.txt", "Hello, Xylem from Native QuickJS!", 0, 0);
console.log("tee result code:", helloResult.code);

const catResult = db.cat("/hello.txt", 0, 0);
console.log("cat result code:", catResult.code);
const rowsJson = JSON.parse(catResult.getRowsJson());
console.log("rows JSON: " + Object.keys(rowsJson).join(" "));
console.log("Successfully ran test_xylem_native!");
