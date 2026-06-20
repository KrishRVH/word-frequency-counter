use std::cmp::Ordering;
use std::collections::HashMap;
use std::fmt::Write as _;

const DEFAULT_MAX_WORD: usize = 64;
const ESTIMATED_BYTES_PER_UNIQUE_WORD: usize = 32;
const MAX_WORD: usize = 1024;
const MIN_WORD: usize = 4;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Entry {
    pub word: String,
    pub count: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WordCounts {
    pub total: u64,
    pub unique: usize,
    pub top: Vec<Entry>,
}

#[must_use]
pub fn count_words(bytes: &[u8], limit: usize, max_word: usize) -> WordCounts {
    let max_word = normalize_max_word(max_word);
    let mut counts = HashMap::with_capacity(estimated_unique_words(bytes));
    let mut word = String::with_capacity(max_word.min(DEFAULT_MAX_WORD));
    let mut total = 0u64;

    for &byte in bytes {
        if byte.is_ascii_alphabetic() {
            if word.len() < max_word {
                word.push(char::from(byte.to_ascii_lowercase()));
            }
        } else if finish_word(&mut counts, &mut word) {
            total += 1;
        }
    }
    if finish_word(&mut counts, &mut word) {
        total += 1;
    }

    let unique = counts.len();
    let mut top: Vec<Entry> = counts
        .into_iter()
        .map(|(word, count)| Entry { word, count })
        .collect();
    top.sort_unstable_by(compare_entries);
    top.truncate(limit);

    WordCounts { total, unique, top }
}

#[must_use]
pub fn normalize_max_word(max_word: usize) -> usize {
    match max_word {
        0 => DEFAULT_MAX_WORD,
        value => value.clamp(MIN_WORD, MAX_WORD),
    }
}

fn estimated_unique_words(bytes: &[u8]) -> usize {
    bytes.len() / ESTIMATED_BYTES_PER_UNIQUE_WORD
}

fn compare_entries(left: &Entry, right: &Entry) -> Ordering {
    right
        .count
        .cmp(&left.count)
        .then_with(|| left.word.cmp(&right.word))
}

fn finish_word(counts: &mut HashMap<String, u64>, word: &mut String) -> bool {
    if word.is_empty() {
        return false;
    }
    if let Some(count) = counts.get_mut(word.as_str()) {
        *count += 1;
    } else {
        counts.insert(word.clone(), 1);
    }
    word.clear();
    true
}

#[must_use]
pub fn render_text(result: &WordCounts) -> String {
    let mut output = String::with_capacity(32 * result.top.len() + 32);
    output.push_str("count word\n");

    for entry in &result.top {
        let _ = writeln!(output, "{} {}", entry.count, entry.word);
    }
    let _ = writeln!(output, "total {}", result.total);
    let _ = writeln!(output, "unique {}", result.unique);
    output
}

#[must_use]
pub fn render_json(result: &WordCounts) -> String {
    let mut output = String::with_capacity(40 * result.top.len() + 32);
    let _ = write!(
        output,
        "{{\"total\":{},\"unique\":{},\"top\":[",
        result.total, result.unique
    );
    for (index, entry) in result.top.iter().enumerate() {
        if index > 0 {
            output.push(',');
        }
        let _ = write!(
            output,
            "{{\"word\":\"{}\",\"count\":{}}}",
            entry.word, entry.count
        );
    }
    output.push_str("]}");

    output
}
