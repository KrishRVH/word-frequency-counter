package body Wordcount_Scanner
  with SPARK_Mode => On
is
   function Has_Word (Value : State) return Boolean
   is (Value.Length > 0);

   function Word_Length (Value : State) return Natural
   is (Value.Length);

   function Image (Value : State) return String is
      Result : String (1 .. Value.Length) := [others => 'a'];
   begin
      for Index in Result'Range loop
         Result (Index) := Value.Word (Index);
         pragma
           Loop_Invariant
             (for all Position in Result'First .. Index =>
                Result (Position) in 'a' .. 'z');
      end loop;
      return Result;
   end Image;

   procedure Reset (Value : out State) is
   begin
      Value := (Word => [others => 'a'], Length => 0);
   end Reset;

   procedure Consume
     (Value     : in out State;
      Byte      : Wordcount_ASCII.Byte;
      Max_Word  : Wordcount_ASCII.Word_Limit;
      Completed : out Boolean) is
   begin
      Completed := False;

      if Wordcount_ASCII.Is_Letter (Byte) then
         if Value.Length < Natural (Max_Word) then
            Value.Length := Value.Length + 1;
            Value.Word (Value.Length) := Wordcount_ASCII.Lower (Byte);
         end if;
      elsif Value.Length > 0 then
         Completed := True;
      end if;
   end Consume;
end Wordcount_Scanner;
