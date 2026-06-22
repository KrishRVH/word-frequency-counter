use std::env;
use std::fs;
use std::process::ExitCode;
use std::time::Instant;

use word_frequency_counter_rust::{WordCounts, count_words, render_json, render_text};

const CHECKSUM_OFFSET: u32 = 2_166_136_261;
const CHECKSUM_PRIME: u32 = 16_777_619;
const USAGE: &str = "usage: wordcount_rust [--json] [--top N] [--max-word N] <file>";

struct Options {
    path: String,
    top: usize,
    max_word: usize,
    bench_runs: Option<usize>,
    bench_warmups: usize,
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

    if let Some(runs) = options.bench_runs {
        println!(
            "{}",
            bench_json(
                &bytes,
                options.top,
                options.max_word,
                runs,
                options.bench_warmups
            )
        );
        return Ok(());
    }

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
    let mut bench_runs = None;
    let mut bench_warmups = 0usize;
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
        } else if arg == "--bench-runs" {
            let value = args.next().ok_or_else(|| String::from(USAGE))?;
            bench_runs = Some(parse_number(&value, "--bench-runs")?);
        } else if let Some(value) = arg.strip_prefix("--bench-runs=") {
            bench_runs = Some(parse_number(value, "--bench-runs")?);
        } else if arg == "--bench-warmups" {
            let value = args.next().ok_or_else(|| String::from(USAGE))?;
            bench_warmups = parse_number(&value, "--bench-warmups")?;
        } else if let Some(value) = arg.strip_prefix("--bench-warmups=") {
            bench_warmups = parse_number(value, "--bench-warmups")?;
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
    if bench_runs == Some(0) {
        return Err(String::from(USAGE));
    }

    path.map(|path| Options {
        path,
        top,
        max_word,
        bench_runs,
        bench_warmups,
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

fn bench_json(bytes: &[u8], top: usize, max_word: usize, runs: usize, warmups: usize) -> String {
    for _ in 0..warmups {
        let _ = checksum(&count_words(bytes, top, max_word));
    }

    let started = Instant::now();
    let mut checksum_value = CHECKSUM_OFFSET;
    for _ in 0..runs {
        checksum_value = mix_u32(checksum_value, checksum(&count_words(bytes, top, max_word)));
    }
    let mean_ms = started.elapsed().as_secs_f64() * 1_000.0 / runs as f64;

    format!("{{\"mean_ms\":{mean_ms:.6},\"checksum\":{checksum_value}}}")
}

fn checksum(result: &WordCounts<'_>) -> u32 {
    let mut value = CHECKSUM_OFFSET;
    value = mix_u64(value, result.total);
    value = mix_u64(value, result.unique as u64);
    for entry in &result.top {
        for &byte in entry.word.as_ref() {
            value = mix_byte(value, byte);
        }
        value = mix_u64(value, entry.count);
    }
    value
}

fn mix_byte(checksum: u32, byte: u8) -> u32 {
    (checksum ^ u32::from(byte)).wrapping_mul(CHECKSUM_PRIME)
}

fn mix_u32(mut checksum: u32, mut value: u32) -> u32 {
    for _ in 0..4 {
        checksum = mix_byte(checksum, (value & 0xff) as u8);
        value >>= 8;
    }
    checksum
}

fn mix_u64(mut checksum: u32, mut value: u64) -> u32 {
    for _ in 0..8 {
        checksum = mix_byte(checksum, (value & 0xff) as u8);
        value >>= 8;
    }
    checksum
}
