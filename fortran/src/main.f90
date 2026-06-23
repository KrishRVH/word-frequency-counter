module wordcount_support
   use, intrinsic :: iso_fortran_env, only: error_unit, int64
   implicit none
   private

   public :: fail, parse_decimal, starts_with

contains

   subroutine fail(message)
      character(len=*), intent(in) :: message

      write (error_unit, '(a)') message
      stop 1
   end subroutine fail

   function starts_with(value, prefix) result(matches)
      character(len=*), intent(in) :: value
      character(len=*), intent(in) :: prefix
      logical :: matches

      matches = len(value) >= len(prefix)
      if (matches) then
         matches = value(1:len(prefix)) == prefix
      end if
   end function starts_with

   function parse_decimal(text, name) result(value)
      character(len=*), intent(in) :: text
      character(len=*), intent(in) :: name
      integer(int64) :: value

      integer :: digit
      integer :: index

      if (len(text) == 0) then
         call fail('wordcount_fortran: ' // name // ' must be a number')
      end if

      value = 0_int64
      do index = 1, len(text)
         digit = iachar(text(index:index)) - iachar('0')
         if (digit < 0 .or. digit > 9) then
            call fail('wordcount_fortran: ' // name // ' must be a number')
         end if
         if (value > (huge(value) - int(digit, int64)) / 10_int64) then
            call fail('wordcount_fortran: ' // name // ' is too large')
         end if
         value = value * 10_int64 + int(digit, int64)
      end do
   end function parse_decimal

end module wordcount_support

module wordcount_options
   use, intrinsic :: iso_fortran_env, only: int64
   use wordcount_support, only: fail, parse_decimal, starts_with
   implicit none
   private

   public :: options_t, parse_options

   character(len=*), parameter :: usage = &
      'usage: wordcount_fortran [--json] [--top N] [--max-word N] <file>'

   type :: options_t
      character(len=:), allocatable :: path
      integer(int64) :: top = 10_int64
      integer(int64) :: max_word = 1024_int64
      integer(int64) :: bench_runs = 0_int64
      integer(int64) :: bench_warmups = 0_int64
      logical :: json = .false.
   end type options_t

contains

   subroutine parse_options(options)
      type(options_t), intent(out) :: options

      character(len=:), allocatable :: arg
      integer :: argc
      integer :: index

      options = options_t()
      argc = command_argument_count()
      index = 1

      do while (index <= argc)
         arg = command_argument(index)

         if (arg == '--json') then
            options%json = .true.
         else if (arg == '--top') then
            index = index + 1
            if (index > argc) call fail(usage)
            options%top = parse_decimal(command_argument(index), '--top')
         else if (starts_with(arg, '--top=')) then
            options%top = parse_decimal(suffix_after(arg, '--top='), '--top')
         else if (arg == '--max-word') then
            index = index + 1
            if (index > argc) call fail(usage)
            options%max_word = parse_decimal(command_argument(index), '--max-word')
         else if (starts_with(arg, '--max-word=')) then
            options%max_word = parse_decimal(suffix_after(arg, '--max-word='), '--max-word')
         else if (arg == '--bench-runs') then
            index = index + 1
            if (index > argc) call fail(usage)
            options%bench_runs = parse_decimal(command_argument(index), '--bench-runs')
         else if (starts_with(arg, '--bench-runs=')) then
            options%bench_runs = parse_decimal(suffix_after(arg, '--bench-runs='), '--bench-runs')
         else if (arg == '--bench-warmups') then
            index = index + 1
            if (index > argc) call fail(usage)
            options%bench_warmups = parse_decimal(command_argument(index), '--bench-warmups')
         else if (starts_with(arg, '--bench-warmups=')) then
            options%bench_warmups = &
               parse_decimal(suffix_after(arg, '--bench-warmups='), '--bench-warmups')
         else if (starts_with(arg, '-')) then
            call fail(usage)
         else if (.not. allocated(options%path)) then
            options%path = arg
         else
            call fail(usage)
         end if

         index = index + 1
      end do

      if (.not. allocated(options%path) .or. options%top == 0_int64) then
         call fail(usage)
      end if
   end subroutine parse_options

   function command_argument(index) result(value)
      integer, intent(in) :: index
      character(len=:), allocatable :: value

      integer :: length
      integer :: status

      call get_command_argument(index, length=length, status=status)
      if (status > 0) call fail(usage)

      allocate (character(len=length) :: value)
      if (length > 0) then
         call get_command_argument(index, value=value, status=status)
         if (status /= 0) call fail(usage)
      end if
   end function command_argument

   function suffix_after(value, prefix) result(suffix)
      character(len=*), intent(in) :: value
      character(len=*), intent(in) :: prefix
      character(len=:), allocatable :: suffix

      if (len(value) <= len(prefix)) then
         suffix = ''
      else
         suffix = value(len(prefix) + 1:)
      end if
   end function suffix_after

end module wordcount_options

module wordcount_io
   use, intrinsic :: iso_fortran_env, only: int8, int64
   use wordcount_support, only: fail
   implicit none
   private

   public :: read_file_bytes

contains

   subroutine read_file_bytes(path, bytes)
      character(len=*), intent(in) :: path
      integer(int8), allocatable, intent(out) :: bytes(:)

      integer :: status
      integer :: unit
      integer(int64) :: file_size

      inquire (file=path, size=file_size, iostat=status)
      if (status /= 0 .or. file_size < 0_int64) then
         call fail('wordcount_fortran: cannot read ' // path)
      end if
      if (file_size > int(huge(0), int64)) then
         call fail('wordcount_fortran: input file is too large')
      end if

      allocate (bytes(int(file_size)))
      open (newunit=unit, file=path, access='stream', form='unformatted', &
         action='read', status='old', iostat=status)
      if (status /= 0) call fail('wordcount_fortran: cannot read ' // path)

      if (size(bytes) > 0) then
         read (unit, iostat=status) bytes
         if (status /= 0) then
            close (unit)
            call fail('wordcount_fortran: cannot read ' // path)
         end if
      end if

      close (unit)
   end subroutine read_file_bytes

end module wordcount_io

module wordcount_counter
   use, intrinsic :: iso_fortran_env, only: int8, int64
   implicit none
   private

   public :: entry_t, result_t, count_words

   integer, parameter :: default_max_word = 64
   integer, parameter :: estimated_bytes_per_unique_word = 32
   integer, parameter :: max_word_limit = 1024
   integer, parameter :: min_word = 4
   integer(int64), parameter :: hash_modulus = 2147483647_int64

   type :: entry_t
      character(len=:), allocatable :: word
      integer(int64) :: count = 0_int64
   end type entry_t

   type :: result_t
      integer(int64) :: total = 0_int64
      integer :: unique = 0
      type(entry_t), allocatable :: top(:)
   end type result_t

   type :: slot_t
      logical :: used = .false.
      character(len=:), allocatable :: word
      integer(int64) :: count = 0_int64
   end type slot_t

   type :: word_map_t
      type(slot_t), allocatable :: slots(:)
      integer :: used = 0
   contains
      procedure :: add => map_add
      procedure :: init => map_init
      procedure :: insert_count => map_insert_count
      procedure :: resize => map_resize
   end type word_map_t

contains

   function count_words(bytes, top, max_word) result(counted)
      integer(int8), intent(in) :: bytes(:)
      integer(int64), intent(in) :: top
      integer(int64), intent(in) :: max_word
      type(result_t) :: counted

      character(len=:), allocatable :: word
      type(entry_t), allocatable :: entries(:)
      type(entry_t), allocatable :: scratch(:)
      type(word_map_t) :: counts
      integer :: byte
      integer :: index
      integer :: limit
      integer :: max_len
      integer :: word_len

      max_len = normalize_max_word(max_word)
      allocate (character(len=max_len) :: word)
      word_len = 0
      counted%total = 0_int64

      call counts%init(initial_capacity(size(bytes)))

      do index = 1, size(bytes)
         byte = int(bytes(index))
         if (is_ascii_letter(byte)) then
            if (word_len < max_len) then
               word_len = word_len + 1
               word(word_len:word_len) = achar(to_lower_ascii(byte))
            end if
         else if (word_len > 0) then
            call counts%add(word(1:word_len))
            counted%total = counted%total + 1_int64
            word_len = 0
         end if
      end do

      if (word_len > 0) then
         call counts%add(word(1:word_len))
         counted%total = counted%total + 1_int64
      end if

      counted%unique = counts%used
      entries = entries_from_map(counts)
      if (size(entries) > 1) then
         allocate (scratch(size(entries)))
         call merge_sort(entries, scratch, 1, size(entries))
      end if

      limit = min(size(entries), int(min(top, int(huge(0), int64))))
      allocate (counted%top(limit))
      if (limit > 0) counted%top = entries(1:limit)
   end function count_words

   function normalize_max_word(value) result(normalized)
      integer(int64), intent(in) :: value
      integer :: normalized

      if (value == 0_int64) then
         normalized = default_max_word
      else if (value < int(min_word, int64)) then
         normalized = min_word
      else if (value > int(max_word_limit, int64)) then
         normalized = max_word_limit
      else
         normalized = int(value)
      end if
   end function normalize_max_word

   function initial_capacity(byte_count) result(capacity)
      integer, intent(in) :: byte_count
      integer :: capacity
      integer :: target

      target = max(16, 2 * max(1, byte_count / estimated_bytes_per_unique_word))
      capacity = 16
      do while (capacity < target)
         capacity = capacity * 2
      end do
   end function initial_capacity

   function is_ascii_letter(byte) result(letter)
      integer, intent(in) :: byte
      logical :: letter

      letter = (byte >= iachar('A') .and. byte <= iachar('Z')) .or. &
         (byte >= iachar('a') .and. byte <= iachar('z'))
   end function is_ascii_letter

   function to_lower_ascii(byte) result(lower)
      integer, intent(in) :: byte
      integer :: lower

      if (byte >= iachar('A') .and. byte <= iachar('Z')) then
         lower = byte + 32
      else
         lower = byte
      end if
   end function to_lower_ascii

   subroutine map_init(self, capacity)
      class(word_map_t), intent(inout) :: self
      integer, intent(in) :: capacity

      allocate (self%slots(capacity))
      self%used = 0
   end subroutine map_init

   subroutine map_add(self, word)
      class(word_map_t), intent(inout) :: self
      character(len=*), intent(in) :: word

      if ((self%used + 1) * 10 >= size(self%slots) * 7) then
         call self%resize(size(self%slots) * 2)
      end if
      call self%insert_count(word, 1_int64)
   end subroutine map_add

   subroutine map_insert_count(self, word, count)
      class(word_map_t), intent(inout) :: self
      character(len=*), intent(in) :: word
      integer(int64), intent(in) :: count

      integer :: index

      index = slot_index(word, size(self%slots))
      do
         if (.not. self%slots(index)%used) then
            self%slots(index)%used = .true.
            self%slots(index)%word = word
            self%slots(index)%count = count
            self%used = self%used + 1
            return
         end if

         if (self%slots(index)%word == word) then
            self%slots(index)%count = self%slots(index)%count + count
            return
         end if

         index = index + 1
         if (index > size(self%slots)) index = 1
      end do
   end subroutine map_insert_count

   subroutine map_resize(self, new_capacity)
      class(word_map_t), intent(inout) :: self
      integer, intent(in) :: new_capacity

      type(slot_t), allocatable :: old_slots(:)
      integer :: index

      call move_alloc(self%slots, old_slots)
      allocate (self%slots(new_capacity))
      self%used = 0

      do index = 1, size(old_slots)
         if (old_slots(index)%used) then
            call self%insert_count(old_slots(index)%word, old_slots(index)%count)
         end if
      end do
   end subroutine map_resize

   function slot_index(word, capacity) result(index)
      character(len=*), intent(in) :: word
      integer, intent(in) :: capacity
      integer :: index

      index = int(modulo(hash_word(word), int(capacity, int64))) + 1
   end function slot_index

   function hash_word(word) result(hash)
      character(len=*), intent(in) :: word
      integer(int64) :: hash

      integer :: index

      hash = 5381_int64
      do index = 1, len(word)
         hash = modulo(hash * 33_int64 + int(iachar(word(index:index)), int64), &
            hash_modulus)
      end do
   end function hash_word

   function entries_from_map(counts) result(entries)
      type(word_map_t), intent(in) :: counts
      type(entry_t), allocatable :: entries(:)

      integer :: entry_index
      integer :: slot_index_value

      allocate (entries(counts%used))
      entry_index = 0
      do slot_index_value = 1, size(counts%slots)
         if (counts%slots(slot_index_value)%used) then
            entry_index = entry_index + 1
            entries(entry_index)%word = counts%slots(slot_index_value)%word
            entries(entry_index)%count = counts%slots(slot_index_value)%count
         end if
      end do
   end function entries_from_map

   recursive subroutine merge_sort(entries, scratch, left, right)
      type(entry_t), intent(inout) :: entries(:)
      type(entry_t), intent(inout) :: scratch(:)
      integer, intent(in) :: left
      integer, intent(in) :: right

      integer :: middle

      if (left >= right) return
      middle = (left + right) / 2
      call merge_sort(entries, scratch, left, middle)
      call merge_sort(entries, scratch, middle + 1, right)
      call merge_entries(entries, scratch, left, middle, right)
   end subroutine merge_sort

   subroutine merge_entries(entries, scratch, left, middle, right)
      type(entry_t), intent(inout) :: entries(:)
      type(entry_t), intent(inout) :: scratch(:)
      integer, intent(in) :: left
      integer, intent(in) :: middle
      integer, intent(in) :: right

      integer :: cursor
      integer :: left_index
      integer :: right_index

      cursor = left
      left_index = left
      right_index = middle + 1

      do while (left_index <= middle .and. right_index <= right)
         if (entry_before(entries(left_index), entries(right_index))) then
            scratch(cursor) = entries(left_index)
            left_index = left_index + 1
         else
            scratch(cursor) = entries(right_index)
            right_index = right_index + 1
         end if
         cursor = cursor + 1
      end do

      do while (left_index <= middle)
         scratch(cursor) = entries(left_index)
         left_index = left_index + 1
         cursor = cursor + 1
      end do

      do while (right_index <= right)
         scratch(cursor) = entries(right_index)
         right_index = right_index + 1
         cursor = cursor + 1
      end do

      entries(left:right) = scratch(left:right)
   end subroutine merge_entries

   function entry_before(left, right) result(before)
      type(entry_t), intent(in) :: left
      type(entry_t), intent(in) :: right
      logical :: before

      if (left%count /= right%count) then
         before = left%count > right%count
      else
         before = word_less(left%word, right%word)
      end if
   end function entry_before

   function word_less(left, right) result(less)
      character(len=*), intent(in) :: left
      character(len=*), intent(in) :: right
      logical :: less

      integer :: index
      integer :: left_char
      integer :: limit
      integer :: right_char

      limit = min(len(left), len(right))
      do index = 1, limit
         left_char = iachar(left(index:index))
         right_char = iachar(right(index:index))
         if (left_char /= right_char) then
            less = left_char < right_char
            return
         end if
      end do

      less = len(left) < len(right)
   end function word_less

end module wordcount_counter

module wordcount_render
   use, intrinsic :: iso_fortran_env, only: int64, output_unit, real64
   use wordcount_counter, only: count_words, result_t
   use wordcount_options, only: options_t
   implicit none
   private

   public :: render_bench, render_json, render_text

   integer(int64), parameter :: checksum_offset = 2166136261_int64
   integer(int64), parameter :: checksum_prime = 16777619_int64
   integer(int64), parameter :: checksum_modulus = 4294967296_int64

contains

   subroutine render_json(result)
      type(result_t), intent(in) :: result

      integer :: index

      write (output_unit, '(a,i0,a,i0,a)', advance='no') &
         '{"total":', result%total, ',"unique":', result%unique, ',"top":['

      do index = 1, size(result%top)
         if (index > 1) write (output_unit, '(a)', advance='no') ','
         write (output_unit, '(a,a,a,i0,a)', advance='no') &
            '{"word":"', result%top(index)%word, '","count":', &
            result%top(index)%count, '}'
      end do

      write (output_unit, '(a)') ']}'
   end subroutine render_json

   subroutine render_text(result)
      type(result_t), intent(in) :: result

      integer :: index

      write (output_unit, '(a)') 'count word'
      do index = 1, size(result%top)
         write (output_unit, '(i0,1x,a)') result%top(index)%count, &
            result%top(index)%word
      end do
      write (output_unit, '(a,i0)') 'total ', result%total
      write (output_unit, '(a,i0)') 'unique ', result%unique
   end subroutine render_text

   subroutine render_bench(bytes, options)
      use, intrinsic :: iso_fortran_env, only: int8
      integer(int8), intent(in) :: bytes(:)
      type(options_t), intent(in) :: options

      character(len=64) :: mean_text
      integer(int64) :: checksum
      integer(int64) :: finished
      integer(int64) :: rate
      integer(int64) :: run
      integer(int64) :: started
      real(real64) :: mean_ms
      type(result_t) :: result

      do run = 1_int64, options%bench_warmups
         result = count_words(bytes, options%top, options%max_word)
         checksum = checksum_result(result)
      end do

      checksum = checksum_offset
      call system_clock(count=started, count_rate=rate)
      do run = 1_int64, options%bench_runs
         result = count_words(bytes, options%top, options%max_word)
         checksum = mix_u32(checksum, checksum_result(result))
      end do
      call system_clock(count=finished)

      mean_ms = real(finished - started, real64) * 1000.0_real64 / &
         real(rate, real64) / real(options%bench_runs, real64)
      write (mean_text, '(f20.6)') mean_ms
      write (output_unit, '(a,a,a,i0,a)') '{"mean_ms":', &
         trim(adjustl(mean_text)), ',"checksum":', checksum, '}'
   end subroutine render_bench

   function checksum_result(result) result(checksum)
      type(result_t), intent(in) :: result
      integer(int64) :: checksum

      integer :: byte_index
      integer :: entry_index

      checksum = checksum_offset
      checksum = mix_u64(checksum, result%total)
      checksum = mix_u64(checksum, int(result%unique, int64))

      do entry_index = 1, size(result%top)
         do byte_index = 1, len(result%top(entry_index)%word)
            checksum = mix_byte(checksum, &
               iachar(result%top(entry_index)%word(byte_index:byte_index)))
         end do
         checksum = mix_u64(checksum, result%top(entry_index)%count)
      end do
   end function checksum_result

   function mix_byte(checksum, byte) result(mixed)
      integer(int64), intent(in) :: checksum
      integer, intent(in) :: byte
      integer(int64) :: mixed

      mixed = modulo(ieor(checksum, int(byte, int64)) * checksum_prime, &
         checksum_modulus)
   end function mix_byte

   function mix_u32(checksum, value) result(mixed)
      integer(int64), intent(in) :: checksum
      integer(int64), intent(in) :: value
      integer(int64) :: mixed

      integer :: index
      integer(int64) :: remaining

      mixed = checksum
      remaining = value
      do index = 1, 4
         mixed = mix_byte(mixed, int(modulo(remaining, 256_int64)))
         remaining = remaining / 256_int64
      end do
   end function mix_u32

   function mix_u64(checksum, value) result(mixed)
      integer(int64), intent(in) :: checksum
      integer(int64), intent(in) :: value
      integer(int64) :: mixed

      integer :: index
      integer(int64) :: remaining

      mixed = checksum
      remaining = value
      do index = 1, 8
         mixed = mix_byte(mixed, int(modulo(remaining, 256_int64)))
         remaining = remaining / 256_int64
      end do
   end function mix_u64

end module wordcount_render

program wordcount_fortran
   use, intrinsic :: iso_fortran_env, only: int8, int64
   use wordcount_counter, only: count_words, result_t
   use wordcount_io, only: read_file_bytes
   use wordcount_options, only: options_t, parse_options
   use wordcount_render, only: render_bench, render_json, render_text
   implicit none

   integer(int8), allocatable :: bytes(:)
   type(options_t) :: options
   type(result_t) :: result

   call parse_options(options)
   call read_file_bytes(options%path, bytes)

   if (options%bench_runs > 0_int64) then
      call render_bench(bytes, options)
   else
      result = count_words(bytes, options%top, options%max_word)
      if (options%json) then
         call render_json(result)
      else
         call render_text(result)
      end if
   end if
end program wordcount_fortran
