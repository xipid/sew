import { XylemEngine } from "/home/xi/Repo/xylem/include/Xylem/Xylem.hpp";

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
const rowsJson = catResult.getRowsJson();
console.log("rows JSON:", rowsJson);
console.log("Successfully ran test_xylem_native!");
