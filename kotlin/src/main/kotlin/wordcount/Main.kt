package wordcount

import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.util.Locale

private const val ORACLE_DEFAULT_MAX_WORD = 64
private const val DEFAULT_TOP = 10
private const val ESTIMATED_BYTES_PER_UNIQUE_WORD = 32
private const val MAX_WORD = 1024
private const val MIN_WORD = 4
private const val ASCII_CASE_BIT = 32
private const val BYTE_MASK = 0xff
private const val NANOS_PER_MILLISECOND = 1_000_000.0
private const val CHECKSUM_MASK = 0xffff_ffffL
private const val CHECKSUM_OFFSET = 2_166_136_261L
private const val CHECKSUM_PRIME = 16_777_619L
private const val CHECKSUM_BYTE_BYTES = 1
private const val CHECKSUM_U32_BYTES = 4
private const val CHECKSUM_U64_BYTES = 8

data class Entry(
    val word: String,
    val count: Long,
)

data class Result(
    val total: Long,
    val unique: Int,
    val top: List<Entry>,
)

fun countBytes(
    bytes: ByteArray,
    top: Int,
    maxWord: Int,
): Result {
    val normalizedMaxWord = normalizeMaxWord(maxWord)
    val counts = HashMap<String, Long>(bytes.size / ESTIMATED_BYTES_PER_UNIQUE_WORD)
    val word = StringBuilder(minOf(normalizedMaxWord, ORACLE_DEFAULT_MAX_WORD))
    var total = 0L

    for (raw in bytes) {
        val byte = raw.toInt() and BYTE_MASK
        if (isLetter(byte)) {
            if (word.length < normalizedMaxWord) {
                word.append(lowerAscii(byte).toChar())
            }
            continue
        }

        if (word.isNotEmpty()) {
            commitWord(counts, word)
            total += 1L
            word.clear()
        }
    }

    if (word.isNotEmpty()) {
        commitWord(counts, word)
        total += 1L
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
        val bytes = Files.readAllBytes(Path.of(options.path))
        if (options.benchRuns > 0) {
            print(renderBench(bytes, options))
        } else {
            val result = countBytes(bytes, options.top, options.maxWord)
            print(if (options.json) renderJson(result) else renderText(result))
        }
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
    counts: MutableMap<String, Long>,
    word: StringBuilder,
) {
    val key = word.toString()
    counts[key] = (counts[key] ?: 0L) + 1L
}

private fun isLetter(byte: Int): Boolean = byte in 'A'.code..'Z'.code || byte in 'a'.code..'z'.code

private fun lowerAscii(byte: Int): Int = if (byte in 'A'.code..'Z'.code) byte + ASCII_CASE_BIT else byte

private fun normalizeMaxWord(value: Int): Int =
    when {
        value == 0 -> ORACLE_DEFAULT_MAX_WORD
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

private fun renderBench(
    bytes: ByteArray,
    options: Options,
): String {
    fun checksum(result: Result): Long {
        var checksum = CHECKSUM_OFFSET
        checksum = mixChecksum(checksum, result.total, CHECKSUM_U64_BYTES)
        checksum = mixChecksum(checksum, result.unique.toLong(), CHECKSUM_U64_BYTES)
        for (entry in result.top) {
            for (character in entry.word) {
                checksum = mixChecksum(checksum, character.code.toLong(), CHECKSUM_BYTE_BYTES)
            }
            checksum = mixChecksum(checksum, entry.count, CHECKSUM_U64_BYTES)
        }
        return checksum
    }

    repeat(options.benchWarmups) {
        checksum(countBytes(bytes, options.top, options.maxWord))
    }

    var checksumValue = CHECKSUM_OFFSET
    val started = System.nanoTime()
    repeat(options.benchRuns) {
        checksumValue =
            mixChecksum(
                checksumValue,
                checksum(countBytes(bytes, options.top, options.maxWord)),
                CHECKSUM_U32_BYTES,
            )
    }
    val meanMs = (System.nanoTime() - started).toDouble() / NANOS_PER_MILLISECOND / options.benchRuns

    return """{"mean_ms":${"%.6f".format(Locale.ROOT, meanMs)},"checksum":$checksumValue}""" + "\n"
}

private fun mixChecksum(
    checksum: Long,
    value: Long,
    bytes: Int,
): Long {
    var mixed = checksum
    var remaining = value
    repeat(bytes) {
        mixed = ((mixed xor (remaining and BYTE_MASK.toLong())) * CHECKSUM_PRIME) and CHECKSUM_MASK
        remaining = remaining ushr Byte.SIZE_BITS
    }
    return mixed
}

private data class Options(
    val path: String,
    val top: Int,
    val maxWord: Int,
    val benchRuns: Int,
    val benchWarmups: Int,
    val json: Boolean,
) {
    companion object {
        fun parse(args: Array<String>): Options {
            var path: String? = null
            var top = DEFAULT_TOP
            var maxWord = MAX_WORD
            var benchRuns = 0
            var benchWarmups = 0
            var json = false
            val numberOptions =
                mapOf<String, (Int) -> Unit>(
                    "--top" to { top = it },
                    "--max-word" to { maxWord = it },
                    "--bench-runs" to { benchRuns = it },
                    "--bench-warmups" to { benchWarmups = it },
                )
            var index = 0

            while (index < args.size) {
                val arg = args[index]
                val name = arg.substringBefore("=")
                val inlineValue = arg.takeIf { it.contains("=") }?.substringAfter("=")
                val setNumber = numberOptions[name]

                if (name == "--json") {
                    if (inlineValue == null) json = true else usage()
                } else if (setNumber != null) {
                    setNumber(parseNumber(inlineValue ?: args.getOrNull(++index)))
                } else if (arg.startsWith("-")) {
                    usage()
                } else if (path == null) {
                    path = arg
                } else {
                    usage()
                }
                index += 1
            }

            if (path == null || top <= 0) {
                usage()
            }
            return Options(path, top, normalizeMaxWord(maxWord), benchRuns, benchWarmups, json)
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
