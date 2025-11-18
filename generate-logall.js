const fs = require('fs');
const path = require('path');

const inputFile = path.join(__dirname, './src/natives.hpp');
const outputFile = path.join(__dirname, './src/native_hooks/log_all.hpp');

const fileContent = fs.readFileSync(inputFile, 'utf-8');

// Step 1: Extract NativeIndex enum
const enumRegex = /enum class NativeIndex\s*{([\s\S]*?)}/;
const enumMatch = fileContent.match(enumRegex);
if (!enumMatch) {
    console.error('NativeIndex enum not found!');
    process.exit(1);
}

const enumBody = enumMatch[1];
const enumEntries = {};
enumBody.split(',')
    .map(line => line.trim())
    .filter(line => line.length > 0)
    .forEach(line => {
        const [name, value] = line.split('=').map(s => s.trim());
        if (value !== undefined) {
            enumEntries[parseInt(value, 10)] = name;
        }
    });

// Step 2: Parse the file line by line and track current namespace
const lines = fileContent.split(/\r?\n/);
let currentNamespace = null;

let outputLines = [];
outputLines.push('namespace big {')
outputLines.push('native_hooks_logs::native_hooks_logs()');
outputLines.push('{');

let lambdaLines = [];
lambdaLines.push('namespace lua { namespace native {');
lambdaLines.push('    // Logging lambda wrappers');

const funcRegex = /FORCEINLINE\s+constexpr\s+\w+\s+(\w+)\s*\([^)]*\)\s*{[^}]*invoke<\s*(\d+)\s*,/;

lines.forEach(line => {
    line = line.trim();

    // Track namespace
    const nsMatch = line.match(/^namespace\s+(\w+)/);
    if (nsMatch) {
        currentNamespace = nsMatch[1];
    }

    // Match function
    const funcMatch = line.match(funcRegex);
    if (funcMatch && currentNamespace) {
        const funcName = funcMatch[1];
        const index = parseInt(funcMatch[2], 10);
        const enumName = enumEntries[index];
        if (!enumName) return;

        const luaNativeName = `LUA_NATIVE_${currentNamespace.toUpperCase()}_${enumName.toUpperCase()}`;
        outputLines.push(`    native_hooks::add_native_detour(NativeIndex::${enumName}, lua::native::${luaNativeName});`);

        // Generate logging lambda
        // const lambdaName = `${luaNativeName}_LOG`;
        // lambdaLines.push(`    inline auto ${lambdaName} = [](auto&&... args) -> decltype(auto) {`);
        // lambdaLines.push(`        LOG(VERBOSE) << ("[NATIVE] ${currentNamespace.toUpperCase()}::${enumName} called\\n");`);
        // lambdaLines.push(`        return ${luaNativeName}(std::forward<decltype(args)>(args)...);`);
        // lambdaLines.push('    };');
    }
});

outputLines.push('}');
lambdaLines.push('}}');
outputLines.push('}')
fs.writeFileSync(outputFile, outputLines.concat(['']).concat(lambdaLines).join('\n'));
console.log(`log_all.hpp generated with ${outputLines.length - 2} hooks and logging lambdas.`);
