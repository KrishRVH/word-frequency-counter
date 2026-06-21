use std::borrow::Cow;
use std::cmp::Ordering;
use std::collections::HashMap;
use std::collections::hash_map::RandomState;
use std::fmt::Write as _;
use std::sync::LazyLock;

const DEFAULT_MAX_WORD: usize = 64;
const ESTIMATED_BYTES_PER_UNIQUE_WORD: usize = 32;
const MAX_WORD: usize = 1024;
const MIN_WORD: usize = 4;

static HASH_STATE: LazyLock<RandomState> = LazyLock::new(RandomState::new);

type WordMap<'a> = HashMap<Cow<'a, [u8]>, u64, RandomState>;
type CountedWord<'a> = (Cow<'a, [u8]>, u64);

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Entry<'a> {
    pub word: Cow<'a, [u8]>,
    pub count: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WordCounts<'a> {
    pub total: u64,
    pub unique: usize,
    pub top: Vec<Entry<'a>>,
}

#[must_use]
pub fn count_words(bytes: &[u8], limit: usize, max_word: usize) -> WordCounts<'_> {
    let max_word = normalize_max_word(max_word);
    let mut counts =
        WordMap::with_capacity_and_hasher(estimated_unique_words(bytes), (*HASH_STATE).clone());
    let mut folded = Vec::new();
    let mut total = 0u64;
    let mut cursor = 0usize;

    while cursor < bytes.len() {
        while cursor < bytes.len() && !is_letter(bytes[cursor]) {
            cursor += 1;
        }

        let start = cursor;
        let mut stored_len = 0usize;
        let mut needs_fold = false;
        while cursor < bytes.len() && is_letter(bytes[cursor]) {
            if stored_len < max_word {
                needs_fold |= is_uppercase(bytes[cursor]);
                stored_len += 1;
            }
            cursor += 1;
        }

        if stored_len == 0 {
            continue;
        }

        if needs_fold {
            folded.clear();
            for &byte in &bytes[start..start + stored_len] {
                folded.push(lower_ascii(byte));
            }
            finish_owned_word(&mut counts, &folded);
        } else {
            finish_borrowed_word(&mut counts, &bytes[start..start + stored_len]);
        }
        total += 1;
    }

    let unique = counts.len();
    let mut entries: Vec<_> = counts.into_iter().collect();
    entries.sort_unstable_by(compare_counted_words);
    entries.truncate(limit);
    let top = entries
        .into_iter()
        .map(|(word, count)| Entry { word, count })
        .collect();

    WordCounts { total, unique, top }
}

#[must_use]
pub fn normalize_max_word(max_word: usize) -> usize {
    match max_word {
        0 => DEFAULT_MAX_WORD,
        value => value.clamp(MIN_WORD, MAX_WORD),
    }
}

#[inline]
fn estimated_unique_words(bytes: &[u8]) -> usize {
    bytes.len() / ESTIMATED_BYTES_PER_UNIQUE_WORD
}

#[inline]
fn is_letter(byte: u8) -> bool {
    (byte | 0x20).wrapping_sub(b'a') <= b'z' - b'a'
}

#[inline]
fn is_uppercase(byte: u8) -> bool {
    byte.is_ascii_uppercase()
}

#[inline]
fn lower_ascii(byte: u8) -> u8 {
    byte | 0x20
}

#[inline]
fn compare_counted_words(
    (left_word, left_count): &CountedWord<'_>,
    (right_word, right_count): &CountedWord<'_>,
) -> Ordering {
    right_count
        .cmp(left_count)
        .then_with(|| left_word.as_ref().cmp(right_word.as_ref()))
}

#[inline]
fn finish_borrowed_word<'a>(counts: &mut WordMap<'a>, word: &'a [u8]) {
    *counts.entry(Cow::Borrowed(word)).or_insert(0) += 1;
}

#[inline]
fn finish_owned_word(counts: &mut WordMap<'_>, word: &[u8]) {
    if let Some(count) = counts.get_mut(word) {
        *count += 1;
    } else {
        counts.insert(Cow::Owned(word.to_vec()), 1);
    }
}

#[must_use]
pub fn render_text(result: &WordCounts<'_>) -> String {
    let mut output = String::with_capacity(32 * result.top.len() + 32);
    output.push_str("count word\n");

    for entry in &result.top {
        let _ = write!(output, "{} ", entry.count);
        push_ascii_word(&mut output, entry.word.as_ref());
        output.push('\n');
    }
    let _ = writeln!(output, "total {}", result.total);
    let _ = writeln!(output, "unique {}", result.unique);
    output
}

#[must_use]
pub fn render_json(result: &WordCounts<'_>) -> String {
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
        output.push_str("{\"word\":\"");
        push_ascii_word(&mut output, entry.word.as_ref());
        let _ = write!(output, "\",\"count\":{}}}", entry.count);
    }
    output.push_str("]}");

    output
}

fn push_ascii_word(output: &mut String, word: &[u8]) {
    output.extend(word.iter().map(|&byte| char::from(byte)));
}
