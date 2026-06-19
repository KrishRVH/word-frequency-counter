package wordcount

import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path

private const val DEFAULT_MAX_WORD = 64
private const val DEFAULT_TOP = 10
private const val INITIAL_CAPACITY = 16_384
private const val MAX_WORD = 1024
private const val MIN_WORD = 4
private const val ASCII_CASE_BIT = 32
private const val BYTE_MASK = 0xff

data class Entry(
    val word: String,
    val count: ULong,
)

data class Result(
    val total: ULong,
    val unique: Int,
    val top: List<Entry>,
)

fun countBytes(
    bytes: ByteArray,
    top: Int,
    maxWord: Int,
): Result {
    val normalizedMaxWord = normalizeMaxWord(maxWord)
    val counts = HashMap<String, ULong>(INITIAL_CAPACITY)
    val word = StringBuilder(minOf(normalizedMaxWord, DEFAULT_MAX_WORD))
    var total = 0UL
    var inWord = false

    for (raw in bytes) {
        val byte = raw.toInt() and BYTE_MASK
        if (isLetter(byte)) {
            inWord = true
            if (word.length < normalizedMaxWord) {
                word.append(lowerAscii(byte).toChar())
            }
            continue
        }

        if (inWord) {
            commitWord(counts, word)
            total += 1UL
            word.clear()
            inWord = false
        }
    }

    if (inWord) {
        commitWord(counts, word)
        total += 1UL
    }

    val entries =
        counts
            .map { Entry(it.key, it.value) }
            .sortedWith(compareByDescending<Entry> { it.count }.thenBy { it.word })
            .take(top)

    return Result(total, counts.size, entries)
}

fun main(args: Array<String>) {
    try {
        val options = Options.parse(args)
        val result = countBytes(Files.readAllBytes(Path.of(options.path)), options.top, options.maxWord)
        print(if (options.json) renderJson(result) else renderText(result))
    } catch (error: IllegalArgumentException) {
        System.err.println("wordcount_kotlin: ${error.message}")
        kotlin.system.exitProcess(2)
    } catch (error: IOException) {
        System.err.println("wordcount_kotlin: ${error.message}")
        kotlin.system.exitProcess(1)
    } catch (error: SecurityException) {
        System.err.println("wordcount_kotlin: ${error.message}")
        kotlin.system.exitProcess(1)
    }
}

private fun commitWord(
    counts: MutableMap<String, ULong>,
    word: StringBuilder,
) {
    val key = word.toString()
    counts[key] = (counts[key] ?: 0UL) + 1UL
}

private fun isLetter(byte: Int): Boolean {
    val lower = byte or ASCII_CASE_BIT
    return lower in 'a'.code..'z'.code
}

private fun lowerAscii(byte: Int): Int = if (byte in 'A'.code..'Z'.code) byte + ASCII_CASE_BIT else byte

private fun normalizeMaxWord(value: Int): Int =
    when {
        value == 0 -> DEFAULT_MAX_WORD
        value < MIN_WORD -> MIN_WORD
        value > MAX_WORD -> MAX_WORD
        else -> value
    }

private fun renderJson(result: Result): String {
    val top = result.top.joinToString(",") { """{"word":"${it.word}","count":${it.count}}""" }
    return """{"total":${result.total},"unique":${result.unique},"top":[$top]}""" + "\n"
}

private fun renderText(result: Result): String =
    buildString {
        appendLine("count word")
        for (entry in result.top) {
            appendLine("${entry.count} ${entry.word}")
        }
        appendLine("total ${result.total}")
        appendLine("unique ${result.unique}")
    }

private data class Options(
    val path: String,
    val top: Int,
    val maxWord: Int,
    val json: Boolean,
) {
    companion object {
        fun parse(args: Array<String>): Options {
            var path: String? = null
            var top = DEFAULT_TOP
            var maxWord = MAX_WORD
            var json = false
            var index = 0

            while (index < args.size) {
                val arg = args[index]
                when {
                    arg == "--json" -> json = true
                    arg == "--top" -> top = parseNumber(args.getOrNull(++index))
                    arg.startsWith("--top=") -> top = parseNumber(arg.substringAfter("="))
                    arg == "--max-word" -> maxWord = parseNumber(args.getOrNull(++index))
                    arg.startsWith("--max-word=") -> maxWord = parseNumber(arg.substringAfter("="))
                    arg.startsWith("-") -> usage()
                    path == null -> path = arg
                    else -> usage()
                }
                index += 1
            }

            if (path == null || top <= 0) {
                usage()
            }
            return Options(path, top, normalizeMaxWord(maxWord), json)
        }

        private fun parseNumber(value: String?): Int =
            if (value != null && value.isNotEmpty() && value.all { it in '0'..'9' }) {
                value.toIntOrNull() ?: usage()
            } else {
                usage()
            }

        private fun usage(): Nothing =
            throw IllegalArgumentException(
                "usage: wordcount-kotlin [--json] [--top N] [--max-word N] <file>",
            )
    }
}
