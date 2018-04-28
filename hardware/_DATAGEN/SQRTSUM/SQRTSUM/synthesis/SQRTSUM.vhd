-- SQRTSUM.vhd

-- Generated using ACDS version 16.0 211

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity SQRTSUM is
	port (
		areset : in  std_logic                     := '0';             -- areset.reset
		clk    : in  std_logic                     := '0';             --    clk.clk
		q      : out std_logic_vector(18 downto 0);                    --      q.q
		r      : out std_logic_vector(17 downto 0);                    --      r.r
		x      : in  std_logic_vector(11 downto 0) := (others => '0'); --      x.x
		y      : in  std_logic_vector(11 downto 0) := (others => '0')  --      y.y
	);
end entity SQRTSUM;

architecture rtl of SQRTSUM is
	component SQRTSUM_CORDIC_0 is
		port (
			clk    : in  std_logic                     := 'X';             -- clk
			areset : in  std_logic                     := 'X';             -- reset
			x      : in  std_logic_vector(11 downto 0) := (others => 'X'); -- x
			y      : in  std_logic_vector(11 downto 0) := (others => 'X'); -- y
			q      : out std_logic_vector(18 downto 0);                    -- q
			r      : out std_logic_vector(17 downto 0)                     -- r
		);
	end component SQRTSUM_CORDIC_0;

begin

	cordic_0 : component SQRTSUM_CORDIC_0
		port map (
			clk    => clk,    --    clk.clk
			areset => areset, -- areset.reset
			x      => x,      --      x.x
			y      => y,      --      y.y
			q      => q,      --      q.q
			r      => r       --      r.r
		);

end architecture rtl; -- of SQRTSUM
