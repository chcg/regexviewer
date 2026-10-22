# Datamodder Regex Viewer

A Notepad++ plugin that shows what a regular expression does. Paste the
regex the way you got it - on its own, inside Python, JavaScript, C#, Java,
PHP, Go, PowerShell or a sed/grep command, or a whole answer with code
blocks - and every match in the open document is highlighted, each group in
its own colour, with the pattern explained in plain language.

- Match like Notepad++ (default), like the source language, or a fixed
  flavour: Python, JavaScript, .NET, Java, PCRE, POSIX ERE/BRE, RE2
- Converted for Notepad++'s Find, with Replace and Replace all
- Runs on Boost.Regex, the same engine as Notepad++'s own Find

**64-bit Notepad++ only.** See [src/help.txt](src/help.txt) for usage.

## Build

Visual Studio C++ build tools and git required. `fetch_vendor.cmd` (called
by the build scripts) fetches Boost.Regex into `vendor\`.

```
build.cmd            compile, close Notepad++, deploy to plugins\RegexViewer, restart
build.cmd nodeploy   compile only (build\RegexViewer.dll)
```

## Test

```
test.cmd
```

Golden tests for extracting regexes from pasted code, the explanation,
matching per flavour (with replacements) and the UTF-8 / ANSI position map.

## License

MIT. Boost.Regex is under the Boost Software License.
