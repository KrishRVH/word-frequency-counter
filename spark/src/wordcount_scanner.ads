with Wordcount_ASCII;

package Wordcount_Scanner
  with SPARK_Mode => On
is
   type State is private;

   function Word_Length (Value : State) return Natural
   with Global => null, Post => Word_Length'Result <= Wordcount_ASCII.Max_Word;

   function Has_Word (Value : State) return Boolean
   with Global => null, Post => Has_Word'Result = (Word_Length (Value) > 0);

   function Image (Value : State) return String
   with
     Global => null,
     Pre    => Has_Word (Value),
     Post   =>
       Image'Result'Length = Word_Length (Value)
       and then (for all Character_Value of Image'Result =>
                   Character_Value in 'a' .. 'z');

   procedure Reset (Value : out State)
   with Global => null, Post => not Has_Word (Value);

   procedure Consume
     (Value     : in out State;
      Byte      : Wordcount_ASCII.Byte;
      Max_Word  : Wordcount_ASCII.Word_Limit;
      Completed : out Boolean)
   with
     Global => null,
     Pre    =>
       (not Has_Word (Value)
        or else Word_Length (Value) <= Natural (Max_Word)),
     Post   =>
       Completed
       = (not Wordcount_ASCII.Is_Letter (Byte) and then Has_Word (Value'Old))
       and then (not Has_Word (Value)
                 or else Word_Length (Value) <= Natural (Max_Word));

private
   subtype Lowercase_Character is Character range 'a' .. 'z';
   type Word_Buffer is array (Positive range <>) of Lowercase_Character;

   type State is record
      Word   : Word_Buffer (1 .. Wordcount_ASCII.Max_Word) := [others => 'a'];
      Length : Natural range 0 .. Wordcount_ASCII.Max_Word := 0;
   end record;
end Wordcount_Scanner;
