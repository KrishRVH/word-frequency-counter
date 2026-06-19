use std::collections::HashMap;
use std::fmt::Write as _;

const DEFAULT_MAX_WORD: usize = 64;
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
    let mut counts: HashMap<String, u64> = HashMap::new();
    let mut word = String::new();
    let mut total = 0u64;
    let max_word = normalize_max_word(max_word);

    for &byte in bytes {
        if byte.is_ascii_alphabetic() {
            if word.len() < max_word {
                word.push(char::from(byte.to_ascii_lowercase()));
            }
        } else {
            finish_word(&mut counts, &mut word, &mut total);
        }
    }
    finish_word(&mut counts, &mut word, &mut total);

    let unique = counts.len();
    let mut top: Vec<Entry> = counts
        .into_iter()
        .map(|(word, count)| Entry { word, count })
        .collect();
    top.sort_unstable_by(|left, right| {
        right
            .count
            .cmp(&left.count)
            .then_with(|| left.word.cmp(&right.word))
    });
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

fn finish_word(counts: &mut HashMap<String, u64>, word: &mut String, total: &mut u64) {
    if word.is_empty() {
        return;
    }

    if let Some(count) = counts.get_mut(word.as_str()) {
        *count += 1;
    } else {
        counts.insert(word.clone(), 1);
    }
    *total += 1;
    word.clear();
}

#[must_use]
pub fn render_text(result: &WordCounts) -> String {
    let mut output = String::from("count word\n");

    for entry in &result.top {
        let _ = writeln!(output, "{} {}", entry.count, entry.word);
    }
    let _ = writeln!(output, "total {}", result.total);
    let _ = writeln!(output, "unique {}", result.unique);
    output
}

#[must_use]
pub fn render_json(result: &WordCounts) -> String {
    let entries = result
        .top
        .iter()
        .map(|entry| format!("{{\"word\":\"{}\",\"count\":{}}}", entry.word, entry.count))
        .collect::<Vec<_>>()
        .join(",");

    format!(
        "{{\"total\":{},\"unique\":{},\"top\":[{}]}}",
        result.total, result.unique, entries
    )
}
