package wordcount

import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.util.Locale
import kotlin.system.exitProcess

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
private const val ERROR_PREFIX = "wordcount_kotlin: "
private const val USAGE =
    "usage: wordcount-kotlin [--json] [--top N] [--max-word N] <file>"

private data class Entry(
    val word: String,
    val count: Long,
)

private data class Result(
    val total: Long,
    val unique: Int,
    val top: List<Entry>,
)

private fun countBytes(
    bytes: ByteArray,
    top: Int,
    maxWord: Int,
): Result {
    val normalizedMaxWord = normalizeMaxWord(maxWord)
    val counts = HashMap<String, Long>(bytes.size / ESTIMATED_BYTES_PER_UNIQUE_WORD)
    val word = StringBuilder(minOf(a = normalizedMaxWord, b = ORACLE_DEFAULT_MAX_WORD))
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
            .asSequence()
            .map { Entry(it.key, it.value) }
            .sortedWith(compareByDescending<Entry> { it.count }.thenBy { it.word })
            .take(top)
            .toList()

    return Result(total = total, unique = counts.size, top = entries)
}

/** Runs the Kotlin command-line implementation. */
fun main(args: Array<String>) {
    try {
        val options = parseOptions(args)
        val bytes = Files.readAllBytes(Path.of(options.path))
        if (options.benchRuns > 0) {
            System.out.print(renderBench(bytes, options))
        } else {
            val result = countBytes(bytes = bytes, top = options.top, maxWord = options.maxWord)
            System.out.print(if (options.isJson) renderJson(result) else renderText(result))
        }
    } catch (error: IllegalArgumentException) {
        System.err.println(ERROR_PREFIX + errorMessage(error))
        exitProcess(2)
    } catch (error: IOException) {
        System.err.println(ERROR_PREFIX + errorMessage(error))
        exitProcess(1)
    } catch (error: SecurityException) {
        System.err.println(ERROR_PREFIX + errorMessage(error))
        exitProcess(1)
    }
}

private fun errorMessage(error: Exception): String = error.message ?: error.javaClass.simpleName

private fun commitWord(
    counts: MutableMap<String, Long>,
    word: StringBuilder,
) {
    val key = word.toString()
    counts[key] = (counts[key] ?: 0L) + 1L
}

private fun isLetter(byte: Int): Boolean = byte in 'A'.code..'Z'.code || byte in 'a'.code..'z'.code

private fun lowerAscii(byte: Int): Int {
    val isUppercase = byte in 'A'.code..'Z'.code
    return if (isUppercase) byte + ASCII_CASE_BIT else byte
}

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
        checksum(countBytes(bytes = bytes, top = options.top, maxWord = options.maxWord))
    }

    var checksumValue = CHECKSUM_OFFSET
    val started = System.nanoTime()
    repeat(options.benchRuns) {
        checksumValue =
            mixChecksum(
                checksum = checksumValue,
                value =
                    checksum(
                        countBytes(bytes = bytes, top = options.top, maxWord = options.maxWord),
                    ),
                bytes = CHECKSUM_U32_BYTES,
            )
    }
    val meanMs =
        (System.nanoTime() - started).toDouble() / NANOS_PER_MILLISECOND / options.benchRuns

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
        mixed = (mixed xor (remaining and BYTE_MASK.toLong())) * CHECKSUM_PRIME and CHECKSUM_MASK
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
    val isJson: Boolean,
)

private class ArgumentCursor(
    private val arguments: Array<String>,
) {
    private var index = 0

    fun hasNext(): Boolean = index < arguments.size

    fun next(): String = arguments.getOrNull(index++) ?: usage()

    fun value(inlineValue: String?): Int = parseNumber(inlineValue ?: next())
}

private fun parseOptions(args: Array<String>): Options {
    val cursor = ArgumentCursor(args)
    var options =
        Options(
            path = "",
            top = DEFAULT_TOP,
            maxWord = MAX_WORD,
            benchRuns = 0,
            benchWarmups = 0,
            isJson = false,
        )

    while (cursor.hasNext()) {
        options = parseArgument(options, cursor.next(), cursor)
    }
    if (options.path.isEmpty() || options.top <= 0) {
        usage()
    }
    return options.copy(maxWord = normalizeMaxWord(options.maxWord))
}

private fun parseArgument(
    options: Options,
    argument: String,
    cursor: ArgumentCursor,
): Options {
    val name = argument.substringBefore("=")
    val inlineValue = argument.takeIf { it.contains("=") }?.substringAfter("=")
    return when (name) {
        "--json" -> if (inlineValue == null) options.copy(isJson = true) else usage()
        "--top" -> options.copy(top = cursor.value(inlineValue))
        "--max-word" -> options.copy(maxWord = cursor.value(inlineValue))
        "--bench-runs" -> options.copy(benchRuns = cursor.value(inlineValue))
        "--bench-warmups" -> options.copy(benchWarmups = cursor.value(inlineValue))
        else -> parsePath(options, argument)
    }
}

private fun parsePath(
    options: Options,
    argument: String,
): Options =
    if (argument.startsWith("-") || options.path.isNotEmpty()) {
        usage()
    } else {
        options.copy(path = argument)
    }

private fun parseNumber(value: String): Int =
    if (value.isNotEmpty() && value.all { it in '0'..'9' }) {
        value.toIntOrNull() ?: usage()
    } else {
        usage()
    }

private fun usage(): Nothing = throw IllegalArgumentException(USAGE)
