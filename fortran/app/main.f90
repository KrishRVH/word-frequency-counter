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
