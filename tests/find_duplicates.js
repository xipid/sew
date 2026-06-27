const fs = require('fs');
const content = fs.readFileSync('/home/xi/Repo/sew/sew_bridge.js', 'utf8');

const decls = {};
const lines = content.split('\n');

for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    let match = line.match(/^\s*(?:export\s+)?(?:class|function|let|const|var)\s+([a-zA-Z0-9_]+)/);
    if (match) {
        const name = match[1];
        if (decls[name]) {
            console.log(`Duplicate declaration found: '${name}' at line ${i + 1} (previous at line ${decls[name]})`);
        } else {
            decls[name] = i + 1;
        }
    }
}
console.log("Check complete.");
