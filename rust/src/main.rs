use std::env;
use std::fs;
use std::process::ExitCode;

use word_frequency_counter_rust::{count_words, render_json, render_text};

const USAGE: &str = "usage: wordcount_rust [--json] [--top N] [--max-word N] <file>";

struct Options {
    path: String,
    top: usize,
    max_word: usize,
    json: bool,
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("{message}");
            ExitCode::FAILURE
        },
    }
}

fn run() -> Result<(), String> {
    let options = parse_args(env::args().skip(1))?;
    let bytes = fs::read(&options.path)
        .map_err(|error| format!("wordcount_rust: cannot read {}: {error}", options.path))?;
    let result = count_words(&bytes, options.top, options.max_word);

    if options.json {
        println!("{}", render_json(&result));
    } else {
        print!("{}", render_text(&result));
    }

    Ok(())
}

fn parse_args(args: impl Iterator<Item = String>) -> Result<Options, String> {
    let mut path = None;
    let mut top = 10usize;
    let mut max_word = 1024usize;
    let mut json = false;
    let mut args = args;

    while let Some(arg) = args.next() {
        if arg == "--json" {
            json = true;
        } else if arg == "--top" {
            let value = args.next().ok_or_else(|| String::from(USAGE))?;
            top = parse_number(&value, "--top")?;
        } else if let Some(value) = arg.strip_prefix("--top=") {
            top = parse_number(value, "--top")?;
        } else if arg == "--max-word" {
            let value = args.next().ok_or_else(|| String::from(USAGE))?;
            max_word = parse_number(&value, "--max-word")?;
        } else if let Some(value) = arg.strip_prefix("--max-word=") {
            max_word = parse_number(value, "--max-word")?;
        } else if arg.starts_with('-') {
            return Err(String::from(USAGE));
        } else if path.is_none() {
            path = Some(arg);
        } else {
            return Err(String::from(USAGE));
        }
    }

    if top == 0 {
        return Err(String::from(USAGE));
    }

    path.map(|path| Options {
        path,
        top,
        max_word,
        json,
    })
    .ok_or_else(|| String::from(USAGE))
}

fn parse_number(value: &str, name: &str) -> Result<usize, String> {
    if value.is_empty() || !value.bytes().all(|byte| byte.is_ascii_digit()) {
        return Err(format!("wordcount_rust: {name} must be a number"));
    }
    value
        .parse()
        .map_err(|_| format!("wordcount_rust: {name} must be a number"))
}
