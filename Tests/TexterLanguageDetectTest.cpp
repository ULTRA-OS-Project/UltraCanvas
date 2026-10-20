// TexterLanguageDetectTest.cpp
// Test suite for UltraTexter's "what language is this pasted text" guess
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
//
// Text pasted into a new, empty tab has no file name to go by, so UltraTexter
// picks the highlighting from the text. The guesser has no framework or editor
// dependency, so the test builds from the one source file.

#include "UltraCanvasLanguageDetect.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

static void Expect(const std::string& name, const std::string& text, const std::string& expected) {
    const std::string got = DetectLanguageFromContent(text);
    const bool passed = got == expected;
    std::cerr << (passed ? "PASS" : "FAIL") << ": " << name;
    if (!passed) std::cerr << " (expected \"" << expected << "\", got \"" << got << "\")";
    std::cerr << std::endl;
    if (!passed) failCount++;
    testCount++;
}

int main() {
    // ===== #! LINES =====
    Expect("python shebang", "#!/usr/bin/env python3\nprint('hi')\n", "Python");
    Expect("bash shebang", "#!/bin/bash\necho hi\n", "Shell Script");
    Expect("env -S node", "#!/usr/bin/env -S node --no-warnings\nx()\n", "JavaScript");
    Expect("perl shebang", "#!/usr/bin/perl -w\n", "Perl");
    Expect("unknown shebang falls through", "#!/usr/bin/awk -f\n{ print $1 }\n", "");

    // ===== WHOLE DOCUMENTS =====
    Expect("json object", "{\n  \"name\": \"texter\",\n  \"version\": 3,\n  \"tags\": [\"a\", \"b\"]\n}\n", "JSON");
    Expect("json array", "[{\"a\": 1}, {\"a\": 2}]", "JSON");
    Expect("jsonc", "{\n  // the port\n  \"port\": 8080,\n}\n", "JSONC");
    Expect("geojson", "{\"type\": \"FeatureCollection\", \"features\": []}", "GeoJSON");
    Expect("ini section is not json", "[section]\nkey=value\n", "");
    Expect("php", "<?php\necho 'hi';\n", "PHP");
    Expect("xml declaration", "<?xml version=\"1.0\"?>\n<config><item/></config>\n", "XML");
    Expect("xml without declaration", "<config>\n  <item name=\"a\"/>\n</config>", "XML");
    Expect("html doctype", "<!DOCTYPE html>\n<html><body><p>Hi</p></body></html>", "HTML");
    Expect("html fragment", "<div class=\"x\">\n  <p>Hello</p>\n</div>", "HTML");
    Expect("svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"><circle r=\"4\"/></svg>", "SVG");
    Expect("rss", "<?xml version=\"1.0\"?>\n<rss version=\"2.0\"><channel></channel></rss>", "RSS/Atom");

    // ===== LINE VOTES =====
    Expect("c++", R"x(#include <iostream>
#include <vector>

int main() {
    std::vector<int> v{1, 2, 3};
    for (auto x : v) std::cout << x << "\n";
    return 0;
}
)x", "C++");
    Expect("c", R"x(#include <stdio.h>
#include <stdlib.h>

typedef struct { int x; } Point;

int main(void) {
    char *p = malloc(10);
    printf("%s\n", p);
    return 0;
}
)x", "C");
    Expect("c++ class", R"x(class Widget {
public:
    Widget() = default;
private:
    int size = 0;
};
)x", "C++");
    Expect("python", R"x(import os
from pathlib import Path

class Loader:
    def __init__(self, root):
        self.root = Path(root)

    def load(self):
        # read every file
        for p in self.root.iterdir():
            if p.is_file():
                yield p.read_text()

if __name__ == "__main__":
    print(list(Loader(".").load()))
)x", "Python");
    Expect("python snippet", "def add(a, b):\n    return a + b\n", "Python");
    Expect("java", R"x(package com.example;

import java.util.List;

public class Main {
    @Override
    public String toString() { return "x"; }
    public static void main(String[] args) {
        System.out.println("hi");
    }
}
)x", "Java");
    Expect("c#", R"x(using System;
using System.Collections.Generic;

namespace Demo
{
    public class Person
    {
        public string Name { get; set; }
        static void Main(string[] args) { Console.WriteLine("hi"); }
    }
}
)x", "C#");
    Expect("javascript", R"x(const fs = require('fs');

function readAll(dir) {
    return fs.readdirSync(dir).map((f) => f.toUpperCase());
}

module.exports = { readAll };
console.log(readAll('.'));
)x", "JavaScript");
    Expect("typescript", R"x(import { readFileSync } from "fs";

export interface Options {
    path: string;
    verbose: boolean;
}

export function load(opts: Options): string {
    return readFileSync(opts.path, "utf8");
}
)x", "TypeScript");
    Expect("go", R"x(package main

import (
    "fmt"
    "os"
)

func main() {
    data, err := os.ReadFile("x")
    if err != nil {
        panic(err)
    }
    fmt.Println(string(data))
}
)x", "Go");
    Expect("rust", R"x(use std::collections::HashMap;

#[derive(Debug)]
struct Point { x: i32 }

fn main() {
    let mut map = HashMap::new();
    map.insert(1, Point { x: 2 });
    println!("{:?}", map);
}
)x", "Rust");
    Expect("ruby", R"x(require 'json'

class Person < Struct
  attr_accessor :name

  def greet
    puts "Hello #{name}"
  end
end

[1, 2].each do |x|
  puts x
end
)x", "Ruby");
    Expect("shell", R"x(# build everything
set -e
if [ -d build ]; then
  rm -rf build
fi
mkdir build && cd build
for f in *.txt; do
  echo "$f"
done
)x", "Shell Script");
    Expect("sql", R"x(CREATE TABLE users (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL
);

SELECT id, name FROM users
WHERE name LIKE 'a%'
ORDER BY name;
)x", "SQL");
    Expect("css", R"x(body, html {
  margin: 0;
  padding: 0;
}

.menu > li a:hover {
  color: #333;
  background: url("x.png");
}

@media (max-width: 600px) {
  .menu { display: none; }
}
)x", "CSS");
    Expect("yaml", R"x(# CI
name: build
on:
  push:
    branches:
      - main
jobs:
  test:
    runs-on: ubuntu-latest
)x", "YAML");
    Expect("lua", R"x(local M = {}

local function helper(x)
  if x ~= nil then
    return x * 2
  end
end

function M.run(t)
  for i = 1, #t do
    print(helper(t[i]))
  end
end

return M
)x", "Lua");
    Expect("kotlin", R"x(import kotlin.math.max

fun main() {
    val xs = listOf(1, 2, 3)
    println(xs.maxOrNull())
}
)x", "Kotlin");
    Expect("swift", R"x(import Foundation

func greet(name: String) -> String {
    guard !name.isEmpty else { return "" }
    return "Hello \(name)"
}
)x", "Swift");
    Expect("perl", "use strict;\nuse warnings;\nmy $x = 1;\nsub hi {\n  print \"hi\";\n}\n", "Perl");
    Expect("php without opening tag", "class A {\n  function run($x) {\n    return $this->go($x);\n  }\n}\n", "PHP");
    Expect("pascal", "program Hello;\nuses crt;\nbegin\n  writeln('Hello');\nend.\n", "Pascal");
    Expect("elixir", "defmodule Math do\n  def sum(a, b) do\n    a + b\n  end\nend\n", "Elixir");

    // ===== MARKDOWN =====
    Expect("markdown readme", R"x(# UltraTexter

A text editor.

## Building

1. Install the dependencies
2. Run **cmake**

See [the docs](Docs/README.md).
)x", "Markdown");
    Expect("markdown with a python fence", R"x(# Example

Here is some code:

```python
import os
def main():
    print(os.getcwd())
if __name__ == "__main__":
    main()
```
)x", "Markdown");
    Expect("markdown table", "| a | b |\n|---|---|\n| 1 | 2 |\n", "Markdown");
    Expect("setext heading", "Title\n=====\n\nSome text.\n", "");

    // ===== NOT A LANGUAGE =====
    Expect("empty", "", "");
    Expect("whitespace", "  \n\t\n", "");
    Expect("one word", "hello", "");
    Expect("prose", "Dear Anna,\n\nThanks for the notes from Tuesday. I will send the draft on Friday.\n\nBest,\nTom\n", "");
    Expect("prose with a colon", "Note: the meeting moved.\nPlease bring your laptop.\n", "");
    Expect("a number", "42", "");
    Expect("url", "https://example.com/a?b=c", "");

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
