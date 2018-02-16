#!/usr/bin/perl -w
# (c) 2001, Dave Jones. (the file handling bit)
# (c) 2005, Joel Schopp <jschopp@austin.ibm.com> (the ugly bit)
# (c) 2007,2008, Andy Whitcroft <apw@uk.ibm.com> (new conditions, test suite)
# (c) 2008-2010 Andy Whitcroft <apw@canonical.com>
# Licensed under the terms of the GNU GPL License version 2

use strict;
use POSIX;

use constant BEFORE_SHORTTEXT => 0;
use constant IN_SHORTTEXT_BLANKLINE => 1;
use constant IN_SHORTTEXT => 2;
use constant AFTER_SHORTTEXT => 3;
use constant CHECK_NEXT_SHORTTEXT => 4;
use constant SHORTTEXT_LIMIT => 75;

my $P = $0;
$P =~ s@(.*)/@@g;
my $D = $1;

my $V = '0.32';

use Getopt::Long qw(:config no_auto_abbrev);

my $quiet = 0;
my $tree = 1;
my $chk_signoff = 1;
my $chk_patch = 1;
my $chk_author = 1;
my $tst_only;
my $emacs = 0;
my $terse = 0;
my $file = 0;
my $check = 0;
my $check_orig = 0;
my $summary = 1;
my $mailback = 0;
my $summary_file = 0;
my $show_types = 0;
my $fix = 0;
my $fix_inplace = 0;
my $root;
my %debug;
my %camelcase = ();
my %use_type = ();
my @use = ();
my %ignore_type = ();
my @ignore = ();
my $help = 0;
my $configuration_file = ".checkpatch.conf";
my $max_line_length = 80;
my $ignore_perl_version = 0;
my $minimum_perl_version = 5.10.0;
my $min_conf_desc_length = 4;
my $spelling_file = "$D/spelling.txt";

sub help {
	my ($exitcode) = @_;

	print << "EOM";
Usage: $P [OPTION]... [FILE]...
Version: $V

Options:
  -q, --quiet                quiet
  --no-tree                  run without a kernel tree
  --no-signoff               do not check for 'Signed-off-by' line
  --no-author                do not check for unexpected authors
  --patch                    treat FILE as patchfile (default)
  --emacs                    emacs compile window format
  --terse                    one line per report
  -f, --file                 treat FILE as regular source file
  --subjective, --strict     enable more subjective tests
  --types TYPE(,TYPE2...)    show only these comma separated message types
  --ignore TYPE(,TYPE2...)   ignore various comma separated message types
  --max-line-length=n        set the maximum line length, if exceeded, warn
  --min-conf-desc-length=n   set the min description length, if shorter, warn
  --show-types               show the message "types" in the output
  --root=PATH                PATH to the kernel tree root
  --no-summary               suppress the per-file summary
  --mailback                 only produce a report in case of warnings/errors
  --summary-file             include the filename in summary
  --debug KEY=[0|1]          turn on/off debugging of KEY, where KEY is one of
                             'values', 'possible', 'type', and 'attr' (default
                             is all off)
  --test-only=WORD           report only warnings/errors containing WORD
                             literally
  --fix                      EXPERIMENTAL - may create horrible results
                             If correctable single-line errors exist, create
                             "<inputfile>.EXPERIMENTAL-checkpatch-fixes"
                             with potential errors corrected to the preferred
                             checkpatch style
  --fix-inplace              EXPERIMENTAL - may create horrible results
                             Is the same as --fix, but overwrites the input
                             file.  It's your fault if there's no backup or git
  --ignore-perl-version      override checking of perl version.  expect
                             runtime errors.
  -h, --help, --version      display this help and exit

When FILE is - read standard input.
EOM

	exit($exitcode);
}

my $conf = which_conf($configuration_file);
if (-f $conf) {
	my @conf_args;
	open(my $conffile, '<', "$conf")
	    or warn "$P: Can't find a readable $configuration_file file $!\n";

	while (<$conffile>) {
		my $line = $_;

		$line =~ s/\s*\n?$//g;
		$line =~ s/^\s*//g;
		$line =~ s/\s+/ /g;

		next if ($line =~ m/^\s*#/);
		next if ($line =~ m/^\s*$/);

		my @words = split(" ", $line);
		foreach my $word (@words) {
			last if ($word =~ m/^#/);
			push (@conf_args, $word);
		}
	}
	close($conffile);
	unshift(@ARGV, @conf_args) if @conf_args;
}

GetOptions(
	'q|quiet+'	=> \$quiet,
	'tree!'		=> \$tree,
	'signoff!'	=> \$chk_signoff,
	'patch!'	=> \$chk_patch,
	'author!'	=> \$chk_author,
	'emacs!'	=> \$emacs,
	'terse!'	=> \$terse,
	'f|file!'	=> \$file,
	'subjective!'	=> \$check,
	'strict!'	=> \$check,
	'ignore=s'	=> \@ignore,
	'types=s'	=> \@use,
	'show-types!'	=> \$show_types,
	'max-line-length=i' => \$max_line_length,
	'min-conf-desc-length=i' => \$min_conf_desc_length,
	'root=s'	=> \$root,
	'summary!'	=> \$summary,
	'mailback!'	=> \$mailback,
	'summary-file!'	=> \$summary_file,
	'fix!'		=> \$fix,
	'fix-inplace!'	=> \$fix_inplace,
	'ignore-perl-version!' => \$ignore_perl_version,
	'debug=s'	=> \%debug,
	'test-only=s'	=> \$tst_only,
	'h|help'	=> \$help,
	'version'	=> \$help
) or help(1);

help(0) if ($help);

$fix = 1 if ($fix_inplace);
$check_orig = $check;

my $exit = 0;

if ($^V && $^V lt $minimum_perl_version) {
	printf "$P: requires at least perl version %vd\n", $minimum_perl_version;
	if (!$ignore_perl_version) {
		exit(1);
	}
}

if ($#ARGV < 0) {
	print "$P: no input files\n";
	exit(1);
}

sub hash_save_array_words {
	my ($hashRef, $arrayRef) = @_;

	my @array = split(/,/, join(',', @$arrayRef));
	foreach my $word (@array) {
		$word =~ s/\s*\n?$//g;
		$word =~ s/^\s*//g;
		$word =~ s/\s+/ /g;
		$word =~ tr/[a-z]/[A-Z]/;

		next if ($word =~ m/^\s*#/);
		next if ($word =~ m/^\s*$/);

		$hashRef->{$word}++;
	}
}

sub hash_show_words {
	my ($hashRef, $prefix) = @_;

	if ($quiet == 0 && keys %$hashRef) {
		print "NOTE: $prefix message types:";
		foreach my $word (sort keys %$hashRef) {
			print " $word";
		}
		print "\n\n";
	}
}

hash_save_array_words(\%ignore_type, \@ignore);
hash_save_array_words(\%use_type, \@use);

my $dbg_values = 0;
my $dbg_possible = 0;
my $dbg_type = 0;
my $dbg_attr = 0;
for my $key (keys %debug) {
	## no critic
	eval "\${dbg_$key} = '$debug{$key}';";
	die "$@" if ($@);
}

my $rpt_cleaners = 0;

if ($terse) {
	$emacs = 1;
	$quiet++;
}

if ($tree) {
	if (defined $root) {
		if (!top_of_kernel_tree($root)) {
			die "$P: $root: --root does not point at a valid tree\n";
		}
	} else {
		if (top_of_kernel_tree('.')) {
			$root = '.';
		} elsif ($0 =~ m@(.*)/scripts/[^/]*$@ &&
						top_of_kernel_tree($1)) {
			$root = $1;
		}
	}

	if (!defined $root) {
		print "Must be run from the top-level dir. of a kernel tree\n";
		exit(2);
	}
}

my $emitted_corrupt = 0;

our $Ident	= qr{
			[A-Za-z_][A-Za-z\d_]*
			(?:\s*\#\#\s*[A-Za-z_][A-Za-z\d_]*)*
		}x;
our $Storage	= qr{extern|static|asmlinkage};
our $Sparse	= qr{
			__user|
			__kernel|
			__force|
			__iomem|
			__must_check|
			__init_refok|
			__kprobes|
			__ref|
			__rcu
		}x;
our $InitAttributePrefix = qr{__(?:mem|cpu|dev|net_|)};
our $InitAttributeData = qr{$InitAttributePrefix(?:initdata\b)};
our $InitAttributeConst = qr{$InitAttributePrefix(?:initconst\b)};
our $InitAttributeInit = qr{$InitAttributePrefix(?:init\b)};
our $InitAttribute = qr{$InitAttributeData|$InitAttributeConst|$InitAttributeInit};

# Notes to $Attribute:
# We need \b after 'init' otherwise 'initconst' will cause a false positive in a check
our $Attribute	= qr{
			const|
			__percpu|
			__nocast|
			__safe|
			__bitwise__|
			__packed__|
			__packed2__|
			__naked|
			__maybe_unused|
			__always_unused|
			__noreturn|
			__used|
			__cold|
			__noclone|
			__deprecated|
			__read_mostly|
			__kprobes|
			$InitAttribute|
			____cacheline_aligned|
			____cacheline_aligned_in_smp|
			____cacheline_internodealigned_in_smp|
			__weak
		  }x;
our $Modifier;
our $Inline	= qr{inline|__always_inline|noinline|__inline|__inline__};
our $Member	= qr{->$Ident|\.$Ident|\[[^]]*\]};
our $Lval	= qr{$Ident(?:$Member)*};

our $Int_type	= qr{(?i)llu|ull|ll|lu|ul|l|u};
our $Binary	= qr{(?i)0b[01]+$Int_type?};
our $Hex	= qr{(?i)0x[0-9a-f]+$Int_type?};
our $Int	= qr{[0-9]+$Int_type?};
our $Octal	= qr{0[0-7]+$Int_type?};
our $Float_hex	= qr{(?i)0x[0-9a-f]+p-?[0-9]+[fl]?};
our $Float_dec	= qr{(?i)(?:[0-9]+\.[0-9]*|[0-9]*\.[0-9]+)(?:e-?[0-9]+)?[fl]?};
our $Float_int	= qr{(?i)[0-9]+e-?[0-9]+[fl]?};
our $Float	= qr{$Float_hex|$Float_dec|$Float_int};
our $Constant	= qr{$Float|$Binary|$Octal|$Hex|$Int};
our $Assignment	= qr{\*\=|/=|%=|\+=|-=|<<=|>>=|&=|\^=|\|=|=};
our $Compare    = qr{<=|>=|==|!=|<|(?<!-)>};
our $Arithmetic = qr{\+|-|\*|\/|%};
our $Operators	= qr{
			<=|>=|==|!=|
			=>|->|<<|>>|<|>|!|~|
			&&|\|\||,|\^|\+\+|--|&|\||$Arithmetic
		  }x;

our $c90_Keywords = qr{do|for|while|if|else|return|goto|continue|switch|default|case|break}x;

our $NonptrType;
our $NonptrTypeMisordered;
our $NonptrTypeWithAttr;
our $Type;
our $TypeMisordered;
our $Declare;
our $DeclareMisordered;

our $NON_ASCII_UTF8	= qr{
	[\xC2-\xDF][\x80-\xBF]               # non-overlong 2-byte
	|  \xE0[\xA0-\xBF][\x80-\xBF]        # excluding overlongs
	| [\xE1-\xEC\xEE\xEF][\x80-\xBF]{2}  # straight 3-byte
	|  \xED[\x80-\x9F][\x80-\xBF]        # excluding surrogates
	|  \xF0[\x90-\xBF][\x80-\xBF]{2}     # planes 1-3
	| [\xF1-\xF3][\x80-\xBF]{3}          # planes 4-15
	|  \xF4[\x80-\x8F][\x80-\xBF]{2}     # plane 16
}x;

our $UTF8	= qr{
	[\x09\x0A\x0D\x20-\x7E]              # ASCII
	| $NON_ASCII_UTF8
}x;

our $typeTypedefs = qr{(?x:
	(?:__)?(?:u|s|be|le)(?:8|16|32|64)|
	atomic_t
)};

our $logFunctions = qr{(?x:
	printk(?:_ratelimited|_once|)|
	(?:[a-z0-9]+_){1,2}(?:printk|emerg|alert|crit|err|warning|warn|notice|info|debug|dbg|vdbg|devel|cont|WARN)(?:_ratelimited|_once|)|
	WARN(?:_RATELIMIT|_ONCE|)|
	panic|
	MODULE_[A-Z_]+|
	seq_vprintf|seq_printf|seq_puts
)};

our $signature_tags = qr{(?xi:
	Signed-off-by:|
	Acked-by:|
	Tested-by:|
	Reviewed-by:|
	Reported-by:|
	Suggested-by:|
	To:|
	Cc:
)};

our @typeListMisordered = (
	qr{char\s+(?:un)?signed},
	qr{int\s+(?:(?:un)?signed\s+)?short\s},
	qr{int\s+short(?:\s+(?:un)?signed)},
	qr{short\s+int(?:\s+(?:un)?signed)},
	qr{(?:un)?signed\s+int\s+short},
	qr{short\s+(?:un)?signed},
	qr{long\s+int\s+(?:un)?signed},
	qr{int\s+long\s+(?:un)?signed},
	qr{long\s+(?:un)?signed\s+int},
	qr{int\s+(?:un)?signed\s+long},
	qr{int\s+(?:un)?signed},
	qr{int\s+long\s+long\s+(?:un)?signed},
	qr{long\s+long\s+int\s+(?:un)?signed},
	qr{long\s+long\s+(?:un)?signed\s+int},
	qr{long\s+long\s+(?:un)?signed},
	qr{long\s+(?:un)?signed},
);

our @typeList = (
	qr{void},
	qr{(?:(?:un)?signed\s+)?char},
	qr{(?:(?:un)?signed\s+)?short\s+int},
	qr{(?:(?:un)?signed\s+)?short},
	qr{(?:(?:un)?signed\s+)?int},
	qr{(?:(?:un)?signed\s+)?long\s+int},
	qr{(?:(?:un)?signed\s+)?long\s+long\s+int},
	qr{(?:(?:un)?signed\s+)?long\s+long},
	qr{(?:(?:un)?signed\s+)?long},
	qr{(?:un)?signed},
	qr{float},
	qr{double},
	qr{bool},
	qr{struct\s+$Ident},
	qr{union\s+$Ident},
	qr{enum\s+$Ident},
	qr{${Ident}_t},
	qr{${Ident}_handler},
	qr{${Ident}_handler_fn},
	@typeListMisordered,
);
our @typeListWithAttr = (
	@typeList,
	qr{struct\s+$InitAttribute\s+$Ident},
	qr{union\s+$InitAttribute\s+$Ident},
);

our @modifierList = (
	qr{fastcall},
);

our @mode_permission_funcs = (
	["module_param", 3],
	["module_param_(?:array|named|string)", 4],
	["module_param_array_named", 5],
	["debugfs_create_(?:file|u8|u16|u32|u64|x8|x16|x32|x64|size_t|atomic_t|bool|blob|regset32|u32_array)", 2],
	["proc_create(?:_data|)", 2],
	["(?:CLASS|DEVICE|SENSOR)_ATTR", 2],
);

#Create a search pattern for all these functions to speed up a loop below
our $mode_perms_search = "";
foreach my $entry (@mode_permission_funcs) {
	$mode_perms_search .= '|' if ($mode_perms_search ne "");
	$mode_perms_search .= $entry->[0];
}

our $allowed_asm_includes = qr{(?x:
	irq|
	memory|
	time|
	reboot
)};
# memory.h: ARM has a custom one

# Load common spelling mistakes and build regular expression list.
my $misspellings;
my @spelling_list;
my %spelling_fix;
open(my $spelling, '<', $spelling_file)
    or die "$P: Can't open $spelling_file for reading: $!\n";
while (<$spelling>) {
	my $line = $_;

	$line =~ s/\s*\n?$//g;
	$line =~ s/^\s*//g;

	next if ($line =~ m/^\s*#/);
	next if ($line =~ m/^\s*$/);

	my ($suspect, $fix) = split(/\|\|/, $line);

	push(@spelling_list, $suspect);
	$spelling_fix{$suspect} = $fix;
}
close($spelling);
$misspellings = join("|", @spelling_list);

sub build_types {
	my $mods = "(?x:  \n" . join("|\n  ", @modifierList) . "\n)";
	my $all = "(?x:  \n" . join("|\n  ", @typeList) . "\n)";
	my $Misordered = "(?x:  \n" . join("|\n  ", @typeListMisordered) . "\n)";
	my $allWithAttr = "(?x:  \n" . join("|\n  ", @typeListWithAttr) . "\n)";
	$Modifier	= qr{(?:$Attribute|$Sparse|$mods)};
	$NonptrType	= qr{
			(?:$Modifier\s+|const\s+)*
			(?:
				(?:typeof|__typeof__)\s*\([^\)]*\)|
				(?:$typeTypedefs\b)|
				(?:${all}\b)
			)
			(?:\s+$Modifier|\s+const)*
		  }x;
	$NonptrTypeMisordered	= qr{
			(?:$Modifier\s+|const\s+)*
			(?:
				(?:${Misordered}\b)
			)
			(?:\s+$Modifier|\s+const)*
		  }x;
	$NonptrTypeWithAttr	= qr{
			(?:$Modifier\s+|const\s+)*
			(?:
				(?:typeof|__typeof__)\s*\([^\)]*\)|
				(?:$typeTypedefs\b)|
				(?:${allWithAttr}\b)
			)
			(?:\s+$Modifier|\s+const)*
		  }x;
	$Type	= qr{
			$NonptrType
			(?:(?:\s|\*|\[\])+\s*const|(?:\s|\*\s*(?:const\s*)?|\[\])+|(?:\s*\[\s*\])+)?
			(?:\s+$Inline|\s+$Modifier)*
		  }x;
	$TypeMisordered	= qr{
			$NonptrTypeMisordered
			(?:(?:\s|\*|\[\])+\s*const|(?:\s|\*\s*(?:const\s*)?|\[\])+|(?:\s*\[\s*\])+)?
			(?:\s+$Inline|\s+$Modifier)*
		  }x;
	$Declare	= qr{(?:$Storage\s+(?:$Inline\s+)?)?$Type};
	$DeclareMisordered	= qr{(?:$Storage\s+(?:$Inline\s+)?)?$TypeMisordered};
}
build_types();

our $Typecast	= qr{\s*(\(\s*$NonptrType\s*\)){0,1}\s*};

# Using $balanced_parens, $LvalOrFunc, or $FuncArg
# requires at least perl version v5.10.0
# Any use must be runtime checked with $^V

our $balanced_parens = qr/(\((?:[^\(\)]++|(?-1))*\))/;
our $LvalOrFunc	= qr{((?:[\&\*]\s*)?$Lval)\s*($balanced_parens{0,1})\s*};
our $FuncArg = qr{$Typecast{0,1}($LvalOrFunc|$Constant)};

our $declaration_macros = qr{(?x:
	(?:$Storage\s+)?(?:[A-Z_][A-Z0-9]*_){0,2}(?:DEFINE|DECLARE)(?:_[A-Z0-9]+){1,2}\s*\(|
	(?:$Storage\s+)?LIST_HEAD\s*\(|
	(?:$Storage\s+)?${Type}\s+uninitialized_var\s*\(
)};

sub deparenthesize {
	my ($string) = @_;
	return "" if (!defined($string));

	while ($string =~ /^\s*\(.*\)\s*$/) {
		$string =~ s@^\s*\(\s*@@;
		$string =~ s@\s*\)\s*$@@;
	}

	$string =~ s@\s+@ @g;

	return $string;
}

sub seed_camelcase_file {
	my ($file) = @_;

	return if (!(-f $file));

	local $/;

	open(my $include_file, '<', "$file")
	    or warn "$P: Can't read '$file' $!\n";
	my $text = <$include_file>;
	close($include_file);

	my @lines = split('\n', $text);

	foreach my $line (@lines) {
		next if ($line !~ /(?:[A-Z][a-z]|[a-z][A-Z])/);
		if ($line =~ /^[ \t]*(?:#[ \t]*define|typedef\s+$Type)\s+(\w*(?:[A-Z][a-z]|[a-z][A-Z])\w*)/) {
			$camelcase{$1} = 1;
		} elsif ($line =~ /^\s*$Declare\s+(\w*(?:[A-Z][a-z]|[a-z][A-Z])\w*)\s*[\(\[,;]/) {
			$camelcase{$1} = 1;
		} elsif ($line =~ /^\s*(?:union|struct|enum)\s+(\w*(?:[A-Z][a-z]|[a-z][A-Z])\w*)\s*[;\{]/) {
			$camelcase{$1} = 1;
		}
	}
}

my $camelcase_seeded = 0;
sub seed_camelcase_includes {
	return if ($camelcase_seeded);

	my $files;
	my $camelcase_cache = "";
	my @include_files = ();

	$camelcase_seeded = 1;

	if (-e ".git") {
		my $git_last_include_commit = `git log --no-merges --pretty=format:"%h%n" -1 -- include`;
		chomp $git_last_include_commit;
		$camelcase_cache = ".checkpatch-camelcase.git.$git_last_include_commit";
	} else {
		my $last_mod_date = 0;
		$files = `find $root/include -name "*.h"`;
		@include_files = split('\n', $files);
		foreach my $file (@include_files) {
			my $date = POSIX::strftime("%Y%m%d%H%M",
						   localtime((stat $file)[9]));
			$last_mod_date = $date if ($last_mod_date < $date);
		}
		$camelcase_cache = ".checkpatch-camelcase.date.$last_mod_date";
	}

	if ($camelcase_cache ne "" && -f $camelcase_cache) {
		open(my $camelcase_file, '<', "$camelcase_cache")
		    or warn "$P: Can't read '$camelcase_cache' $!\n";
		while (<$camelcase_file>) {
			chomp;
			$camelcase{$_} = 1;
		}
		close($camelcase_file);

		return;
	}

	if (-e ".git") {
		$files = `git ls-files "include/*.h"`;
		@include_files = split('\n', $files);
	}

	foreach my $file (@include_files) {
		seed_camelcase_file($file);
	}

	if ($camelcase_cache ne "") {
		unlink glob ".checkpatch-camelcase.*";
		open(my $camelcase_file, '>', "$camelcase_cache")
		    or warn "$P: Can't write '$camelcase_cache' $!\n";
		foreach (sort { lc($a) cmp lc($b) } keys(%camelcase)) {
			print $camelcase_file ("$_sor<)
		}
		close($camelcase_file);

}
}

sub hit_lommit"_nfo|{
	my ($fommit" $fid,$decsc = @_;

	return i(fid,$decsc =f ($(hich_("it") {eq"") {|| !-e ".git") ;

	my $futput
= `git log --no-mcolr w--ormat:='%H %s'-1 -fommit" 2>&1;
		futput
=  s/^\s*//g;m;	my @lines = split('"n", $mutput
)

	if ($qines [0]=~ /^\rrors:shorte SHA -fommit" s almbiguous\.) {
	# Mybe_one ody cronerstthis hbocakof abashinco spomehisg =tha reaurn s	# ll =mtch-sg =ommit" sds but oi's yersy slow..
V#
#		ehom "hecking oommit"s $1..
"
#		it lrev-ist)--roemtes t| grep -i "^$1" |
#		hile (ead 'ine =; do
#		   oit log --normat:='%H %s'-1 -fine =|
#		   oehom "hmmit" $(ct o-c 1-12,41-)"
#		dne

} elsif ($line  [0]=~ /^\fatal:almbiguous arguent	'$cammit"': unknownlreviion vr watchnot pn the oworing oree\n.) {
	} else {
		mfid= spubtrf$line  [0], 0, 1);
	}	decsc= spubtrf$line  [0], 41;
	}

	ieturn i(fid,$decsc 
}

schk_signoff = 10if ($fixl);

my $@rawines = s);
my @iines = s);
my @iixesd= s);
my @iixesd_nfseted-= s);
my @iixesd_deleed-= s);
my @fix;inesn = "-;

my $Vvame 
for my $kilename i@ARGV, {
	my $lILE 
	if (!file) =
		open(mlILE  '>-|, "$dif =-u /dev/nul =kilename ) {||			die "$P: $rilename : dif =falend -$!\n";
		 elsif ($lilename ieq"'-' =
		open(mlILE  '><&STDIN')
	} else {
		mpen(mlILE  '><, "$file"ame ) {||			die "$P: $rilename : pen $falend -$!\n";
		 	if (!file)ame ieq"'-' =
		oVvame = '.Yur $atch-'
	} else {
		mVvame = 'file)ame 
		 	ihile (<$cILE ) {
			homp;
			ush(@srawines ,$d_;
	}

close($cILE )
	if (!!roc_ess!file)ame ) {
			exit = 01
	}

c@rawines = s);
m	@ines = s);
m	@ixesd= s);
m	iixesd_nfseted-= s);
m	iixesd_deleed-= s);
m	fix;inesn = "-;



sxit($exitc;

sub bop_of_kernel_tree({
	my ($foot) { @_;

	my @aree(_heck = 0(			"COPYING, $"CREDITS, $"Kuild_, $"MAINTAINERS, $"Makeile")
			"README, $"Docuent	tion_, $"rch , $"nclude_, $"driersi)
			"fs, $"nct") $"npc) $"ernel_) $"lib) $"cripts/)
		;

	foreach my $lheck =(aree(_heck  {
		if (!t e "root =. '/'=. $heck  {
		iieturn i0
		}
	}
}ieturn i1
}

sub sarse|_eailb{
	my ($firmat:ed_ceailb { @_;

	my @$ame = '";
	my @$addess t '";
	my @$ammint	' '";
		if (!firmat:ed_ceailb=~ /^\.*)/<(\S+\@\S+)>.*)//) {
		$same = 'f;
		}$addess t '$2
		$cammint	' '$3if (efined $r3
		 elsif ($lirmat:ed_ceailb=~ /^\s*/<(\S+\@\S+)>.*)//) {
		$saddess t '$1
		$cammint	' '$2if (efined $r2
		 elsif ($lirmat:ed_ceailb=~ /^(\S+\@\S+).*)//) {
		$saddess t '$1
		$cammint	' '$2if (efined $r2
			lirmat:ed_ceailb=~ /s/saddess .*$//;		$same = 'firmat:ed_ceailb;		$same = 'trim(same );		$same =  s/^\s"|\"//g;
		$#If chere's na ame =leftafter 'trinppng supacs and 		$#Ilading: qutes  and 'he oaddess toes 't whve Jboth		$#Ilading: nd 'hrilbng: ndge (brckedt, tee oaddess 		$#Is hinalid . ie:		$#I  "joe smth $joe@smth com>"abad		$#I  "joe smth $<joe@smth com>"abad		$f ($lame =e "" && -saddess t~ /(^<[^>]+>$) {
			$came = '";
	m	$saddess t '";
	m	$sammint	' '";
		}
	}

	isame = 'trim(same );		same =  s/^\s"|\"//g;
		saddess t 'trim(saddess )
		saddess t  s/^\s<|\>$/g;

	nf ($lame =~ /^[^\w \-]/i {
 ##as a"ust bqutes" har}s		$same =  s/^?<!-\\)"/\\"g;
 ##cscape qutes 		$same = '"\"/ame \";
	}

	ieturn i(fame ,-saddess ,@$ammint	;
}

sub hirmat:_eailb{
	my ($fame ,-saddess  { @_;

	my @$irmat:ed_ceailb;		isame = 'trim(same );		same =  s/^\s"|\"//g;
		saddess t 'trim(saddess )
		nf ($lame =~ /^[^\w \-]/i {
 ##as a"ust bqutes" har}s		$same =  s/^?<!-\\)"/\\"g;
 ##cscape qutes 		$same = '"\"/ame \";
	}

	if ($"/ame "{eq"") {{			lirmat:ed_ceailb=~"$Paddess ;
	} else {
		mlirmat:ed_ceailb=~"$Pame =<Paddess >;
	}

	ieturn i$irmat:ed_ceailb;	

sub hhich_{
	my ($fbin { @_;

	moreach my $latchn(plit(/\:, $lENV{ATH }) {
		if (!e ".latch/fbin" {
		iieturn i.latch/fbin"
		}
	}

	ieturn "" ;	

sub hhich_conf_{
	my ($fomnf { @_;

	moreach my $latchn(plit(/\:, $".:lENV{HOME}:.cripts/)) {
		if (!e ".latch/fonf")
{
		iieturn i.latch/fonf";
m	}
	}

	ieturn "" ;	

sub hexpnd _tab {
	my ($hsr) . @_;

	my @$rs = s''
	my @$ = 0;
m	or my $kcn(plit(/\, $lsr)  {
		if (!kcneq""\") {
		$	$rs == '| '
	m	$sn+;
	}	fore (;($la % 8) ! 0;
 sn+; {
		$		$rs == '| '
	m	$
		cnext 
		}
		$crs == '$c;		$sa+;
	}
}	ieturn i$re;
}

ub hcopy_upacng: 
		(y @$rs = shift()=~ tr/[\t/ /c;		eturn i$re;
}


ub hine__tat  {
	my ($hine);. @_;

	m# Droptee odif =ine lenaer tnd exipnd 'hab 	$line =~ s/^\.//;		line = $expnd _tab (line);

	p# Pik =he inpent}from the tromt}ff the Gine).	my ($hhict);. @$line =~ /^\\s*@));

	meturn i(ength,(line);, ength,(lhict);;
}

my $rsaitiae|_qutes= s''
	sub seaitiae|_ine__re;et{
	my ($hn_conmint	;= @_;

	if ($qn_conmint	;=
		mlsaitiae|_qutes= s'*/'
	} else {
		mVsaitiae|_qutes= s''
	}
}

ub seaitiae|_ine_{
	my ($hine);. @_;

	my @$rs = s''
	my @$l= s''
	smy @$qeng= 0;
m	y $fuf = 10
	my @$a

	p# Aways_hcopyoverltee odif =marern.	m$rs = shubtrf$line , 0, 1;

	fore($hff = 1;
$fuf =< ength,(line);
$fuf +; {
		$$c= spubtrf$line ,$fuf , 1;

	fp# Cnmint	s w oare waking oommpleelyincludesg =th (begin	fp# nd exnd,$ll tho $;.		if (!ksaitiae|_qutes=eq"''&& -pubtrf$line ,$fuf , 2 {eq"'/*' {
		$	$saitiae|_qutes= s'*/'
			$	pubtrf$lrs ,$duf , 2 "$f;$;";
			$luf +;;		cnext 
		}
		$f (!ksaitiae|_qutes=eq"'*/'&& -pubtrf$line ,$fuf , 2 {eq"'*/' {
		$	$saitiae|_qutes= s''
	m	$pubtrf$lrs ,$duf , 2 "$f;$;";
			$luf +;;		cnext 
		}
		$f (!ksaitiae|_qutes=eq"''&& -pubtrf$line ,$fuf , 2 {eq"'//' {
		$	$saitiae|_qutes= s'//'
			$	pubtrf$lrs ,$duf , 2 "$saitiae|_qutes;
			$luf +;;		cnext 
		}
		fp# A \in a ctring =meansignore vte ne xt har}actrn.	m$f (!!ksaitiae|_qutes=eq""'"{|| ksaitiae|_qutes=eq"'"' {&
			   okcneq""\\) {
		$	pubtrf$lrs ,$duf , 2 "'XX';
			$luf +;;		cnext 
		}
		$# Rgular equtes .		if (!kc=eq""'"{|| kc=eq"'"' {
		$	f (!ksaitiae|_qutes=eq"'' {
		$		$saitiae|_qutes= s$a

	p	$	pubtrf$lrs ,$duf , 1,@$a;
			$next 
		}	 elsif ($lsaitiae|_qutes=eq"$a;{
		$		$saitiae|_qutes= s''
	m	$
		c
		fp#rint "Mc<$c> SQ<$saitiae|_qutes>n";
		ff ($luf =! 0 && kksaitiae|_qutes=eq"'*/'&& -kc=e ""\") {
		$	pubtrf$lrs ,$duf , 1,@$;)
		}
elsif ($luf =! 0 && kksaitiae|_qutes=eq"'//'&& -kc=e ""\") {
		$	pubtrf$lrs ,$duf , 1,@$;)
		}
elsif ($luf =! 0 && kksaitiae|_qutes=& -kc=e ""\") {
		$	pubtrf$lrs ,$duf , 1,@'X';
			 else {
		m	pubtrf$lrs ,$duf , 1,@$a;
			
	}

	if (!ksaitiae|_qutes=eq"'//';=
		mlsaitiae|_qutes= s''
	}
}	i# Te prtchame =o a c#nclude -ay cbesurrognderd by'><,and 'a>'.	if (!krs =  /^\.s*\#\#s*nclude \s+\<.*)/\>/ {
		my $gleane= s'X' x ength,(l1);		$srs =  /s@\<.*\>@<gleane>@;}	i# Te pwholeof a k#rrors s alctring .		 elsif ($lrs =  /^\.s*\#\#s*?:e-rorswarning|\s+(\*)/\b/ {
		my $gleane= s'X' x ength,(l1);		$srs =  /s@(#\#s*?:e-rorswarning|\s+().*@$1gleane;
	}

	$eturn i$re;
}


ub hget_qutesd_tring =
	my ($hine),@$rawines = @_;

	return i" if (!line !~ /m/(\"[X]+\) /);
$return ipubtrf$lrawines,@$-[0], $+[0]=-@$-[0];
}

sub hctx_tat eint	_bocako
	my ($hine)nr,@$reailn,$duf  = @_;
	ry $line = $_inesn =-01
	}y $lblk= s''
	my @$sff = 1duf 
	my @$amf = 1duf =-01
	}y $lamf _st = 0;
m	ry $liuf = 10
		my $tepe = 0''
	my @$lvel d 10
	my @@tatk = 0()
	my @$p
	my @$a

my @$lvn= 10
		my $treailndr;
oihile (<1 {
		m@tatk = 0(['', 0] if ($h#tatk =  "-;;

	fp#arn "$CSB: blk<lblk> reailn<treailn>n";
		f#If cw oare abut aho droptuf =te nxnd,$pul =n aore 		f#Iont|xt .		ff ($luf =>=@$lvn {
		$	ore (;(treailn >0;
 sines+; {
		$		ast if ($defined $rine  [rine ];
			$next if (!line  [rine ]=  /^\-);
			p	treailn--
			p	tiuf = 1$lvn
			p	tblk=.=$rine  [rine ]. "\n)"
			p	tivn= 1ength,(lblk;
			p	tines+;;		$		ast 
	m	$
		cn# Bilb=f there' s ano furhereIont|xt .		fp#arn "$CSB: blk<lblk> uf <luf >1eng<tivn>n";
		fff ($luf =>=@$lvn {
		$		ast 
	m	$
		cnf (!livel d  0 && kpubtrf$lblk,$duf  =  /^\.s*\\#s*efined/;{
		$		$ivel +;;		$		tepe = 0'#'
	m	$
		c
			tp  '$c;		$sc= spubtrf$lblk,$duf , 1;

p	treailner t spubtrf$lblk,$duf ;

	fp#arn "$CSB: c<$c> epe <tepe >1enel <$ivel > reailner <treailner > amf _st <lamf _st >n";

	wn# Hndler=e ted-c#nf/#lse .		ff ($lreailner t  /^\\#s*?:eifnerf|iferf|if\s+/;{
		$	ush(@spatk , [$tepe ,@$lvel d])
		}
elsif ($lreailner t  /^\\#s*?:else|relif\sb/ {
		m	(tepe ,@$lvel  = @_{$patk [h#tatk =-01};
o	}
elsif ($lreailner t  /^\\#s*xndifsb/ {
		m	(tepe ,@$lvel  = @_{pop@spatk )}
		}
		fp# Sat eint	nxnd at lte n';'vr wa lose( '}'at lte 	fp# ut erostl1enel .		ff ($livel d  0 && kkc=eq"';' {
		$	ast 
	m	
		fp# Anelse {i reglly
wa lnditionsalas -ong 2s -t"s ot plse {if		ff ($livel d  0 && kkcmf _st =  0 && 		$		!defined($sp {|| tp   /(?:[s|\*}|\+/) {& 		$		lreailner t  /^\(lse )?:[s|\{)/{& 		$		lreailner t~ /(^lse s+infsb/ {
		m	$amf = 1duf =+ ength,(l1)=-01
	}m	$amf _st = 01
	}m	#arn "$CSB: mare amf <lamf > smf <lsmf > 1<$1>n";
		ff#arn "$[ . jpubtrf$lblk,$dsuf , $amf =-@$sff =+ 1;. "\]n";
		}
		m$f (!!kepe =eq"''&|| tepe =eq"'(' {&
kkc=eq"'(' {
		$	$ivel +;;		$	tepe = 0'('
		}
		$f (!kepe =eq"'('{&
kkc=eq"')' {
		$	$ivel --
			ptepe = 0$livel d! 0 )?"'('{:s''
	sm	ff ($livel d  0 && kkcmf < $dsuf  =
		$		$cff = 1duf 
	m}m	$amf _st = 01
	}m		#arn "$CSB: mare amf <lamf >n";
		ff}		}
		$f (!!kepe =eq"''&|| tepe =eq"'{' {&
kkc=eq"'{' {
		$	$ivel +;;		$	tepe = 0'{'
		}
		$f (!kepe =eq"'{'{&
kkc=eq"'}' {
		$	$ivel --
			ptepe = 0$livel d! 0 )?"'{'{:s''
	sm	ff ($livel d  0  =
		$		f ($pubtrf$lblk,$duf =+ 1, 1;=eq"';' {
		$		$luf +;;		cn$
		cn	ast 
	m	$
		c
		$# Preroc_essorcomma nd aed a tvte ne wines unlss tcscaped.		$f (!kepe =eq"'#'{&
kkc=eq"\n)"{&
kkp=e ""\\) {
		$	$ivel --
			ptepe = 0''
	m	$luf +;;		cnast 
	m	
		$luf +;;		}	i# W oare truy
wat=te nxnd,$so shuffleto the pext iine).	mf ($luf ===@$lvn {
		$tiuf = 1$lvn=+ 1

p	tines+;;		$treailn--
		}		my $ttat eint	t spubtrf$lblk,$dsuf , $mf =-@$sff =+ 1;
	}y $lamditionst spubtrf$lblk,$dsuf , $amf =-@$sff =+ 1;;}	i#arn "$STATEENTA<ttat eint	>n";
		#arn "$CONDIION]<lamditions>n";

	w#rint "Mcmf <lamf > smf <luf >1emf <llmf >n";
		ieturn i(ftat eint	,$lamditions,
	p	tines,@$reailn=+ 1, $mf =-@$luf =+ 1, $lvel  
}

sub seat eint	_ines ={	my ($hsrmt;. @_;

	m# Srinptee odif =ine lrefix s and binptblank ines =at=eatrt nd exnd.	$strm
=  s/^(^|\n)./$1g;
		strm
=  s/^\s*//g
		strm
=  s/^s*$/)/

	my @atrm
_ines = s)strm
=  s/\n/);
$	$eturn i$#trm
_ines =+2;
u

sub seat eint	_rawines ={	my ($hsrmt;. @_;

	my @atrm
_ines = s)strm
=  s/\n/);
$	$eturn i$#trm
_ines =+2;
u

sub seat eint	_bocak_ize {
	my ($strmt;. @_;

	mstrm
=  s/^(^|\n)./$1g;
		strm
=  s/^\s*/{/g
		strm
=  s/^}s*$/)/

	strm
=  s/^\s*//g
		strm
=  s/^s*$/)/

	my @atrm
_ines = s)strm
=  s/\n/);
$my @atrm
_eat eint	 = s)strm
=  s/;/);
$	$y $ttam
_ines = s$#trm
_ines =+2;
u$y $ttam
_eat eint	 = s$#trm
_eat eint	 =+1;

	if (-ttam
_ines =>$ttam
_eat eint	  {
		$eturn $strm
_ines ;	} else {
		meturn $strm
_eat eint	 ;
}
}

sub hctx_tat eint	_ful =
	my ($hine)nr,@$reailn,$duf  = @_;
	ry $(ftat eint	,$lamditions, $lvel  
}	ry $(@chunks;

	p# Grabthe filrtl1lnditionsal/bocakopain.	m(ftat eint	,$lamditions, $lne)nr,@$reailn,$duf ,@$lvel  = 		cn	ctx_tat eint	_bocak$hine)nr,@$reailn,$duf  ;	w#rint "MF: c<$cmditions> s<ttat eint	> reailn<treailn>n";
		ush(@schunks, [$tamditions, $tat eint	t])
	if (!!(treailn >0;&& kkcmditionst  /^\s*(?:u\n[+-])?#s*?:eifelse|rdo\sb/s) {
		$eturn $$livel , $lne)nr,@@chunks;

}
}	i# Pul =n ahe fioloweng oomditionsal/bocakopain and bsee=f thery
f#Ionuld ontinue|the saat eint	.	fore($;; {
		$(ftat eint	,$lamditions, $lne)nr,@$reailn,$duf ,@$lvel  = 		cn	ctx_tat eint	_bocak$hine)nr,@$reailn,$duf  ;	ww#rint "MC: c<$cmditions> s<ttat eint	> reailn<treailn>n";
			ast if ($d(treailn >0;&& kkcmditionst  /^\?:\s*\[n[+-])*#s*?:else|rdo\sb/s) ;	ww#rint "MC: ush(n";
			ush(@schunks, [$tamditions, $tat eint	t])
	i

	ieturn i(fivel , $lne)nr,@@chunks;



sub hctx_bocak_get{
	my ($hine)nr,@$reailn,$duuer, w$pen ,$laose(,$duf  = @_;
	ry $line 
u$y $ttatrt  $_inesn =-01
	}y $lblk= s''
	my @@o
	my @@a

my @@rs = ();

	$y @$lvel d 10
	my @@tatk = 0($lvel  
}fore($hine = $_tatrt;(treailn >0;
 sines+; {
		$ext if (!lrawines [rine ]=  /^\-);
			treailn--
	
p	tblk=.=$rrawines [rine ]

	wn# Hndler=e ted-c#nf/#lse .		ff ($lines [rine ]=  /^\.s*\\#s*?:eifnerf|iferf|if\s+/;{
		$	ush(@spatk , $lvel  
}f} elsif ($line  [rine ]=  /^\.s*\\#s*?:else|relif\sb/ {
		m	$lvel d 1$patk [h#tatk =-01}
}f} elsif ($line  [rine ]=  /^\.s*\\#s*xndifsb/ {
		m	$lvel d 1pop@spatk )
		}
		m$oreach my $lhn(plit(/\, $line  [rine ]; {
		m	##rint "MC<$c>L<$ivel ><$pen laose(>O<luf >n";
		fff ($luf =>0  =
		$		luf --
			p	ext 
		}	 	sm	ff ($lc=eq"$aose( & kklvel d>0  =
		$		livel --
			plast if ($wivel d  0  
		}	 elsif ($lc=eq"$pen ;{
		$		$ivel +;;		$	
		c
		fpf (!$iuuer,&|| tivel d<= 1;{
		$	ush(@srs ,$drawines [rine ])
		}
		m$ast if ($wivel d  0  
		

	ieturn i(fivel , srs )
}

ub hctx_bocak_uuer,&
	my ($hine)nr,@$reailn;. @_;

	my @(fivel , sr;. @ctx_bocak_get$hine)nr,@$reailn,$1,@'{, 't}', 0;
$return i@r
}

ub hctx_bocak&
	my ($hine)nr,@$reailn;. @_;

	my @(fivel , sr;. @ctx_bocak_get$hine)nr,@$reailn,$0,@'{, 't}', 0;
$return i@r
}

ub hctx_tat eint	t
	my ($hine)nr,@$reailn,$duf  = @_;
		my @(fivel , sr;. @ctx_bocak_get$hine)nr,@$reailn,$0,@'(, 't)',$duf  ;	weturn i@r
}

ub hctx_bocak_ivel d
	my ($hine)nr,@$reailn;. @_;

	meturn ictx_bocak_get$hine)nr,@$reailn,$0,@'{, 't}', 0;
$

ub hctx_tat eint	_ivel d
	my ($hine)nr,@$reailn,$duf  = @_;
		meturn ictx_bocak_get$hine)nr,@$reailn,$0,@'(, 't)',$duf  ;	

sub hctx_ocalteconmint	{
	my ($filrtl_ines,@$xnd_ine);. @_;

	m# Ctch sa onmint	{on=te nxndff the Gine)-t"self.	my ($hcurenthconmint	;= @!lrawines [rxnd_ine)=-01}=~ m@(.*(/\*.*\*/)s*(?:u\\s*)?$L@ ;	weturn ihcurenthconmint	 f (defined $rcurenthconmint	;

	m# Loo =heroughthe Gont|xt  nd 'hry nd 'igura outp=f there' s aa
f#Ionmint	.	fy $inc_ammint	' '0
	mhcurenthconmint	  s''
	more($y $line n = "filrtl_ines;$line n =<@$xnd_ine);$line n +; {
		$y $line = $_rawines [rine n =-01}
}f}#arn "$          #rine n";
		ff ($line n =  "filrtl_ines nd 'line =~ m/@\.s*\#*@ {
		m	$nc_ammint	' ';
		}
		cf ($line =~ m/@/#*@ {
		m	$nc_ammint	' ';
		}
		cf ($!$nc_ammint	'& kkcurenthconmint	 e ='' {
		$	hcurenthconmint	  s''
	m}
		$caurenthconmint	 .=$rine . "\n)" f ($qn_conmint	;;		cf ($line =~ m/@\*/@ {
		m	$nc_ammint	' '0
		}
	}
}
	homp;$hcurenthconmint	;;	weturn $hcurenthconmint	;;	

ub hctx_hasconmint	{
	my ($filrtl_ines,@$xnd_ine);. @_;

}y $lam
= hctx_ocalteconmint	$filrtl_ines,@$xnd_ine);

	m##rint "MINE : lrawines [rxnd_ine)=-01 ]n";
		##rint "MCMMT:$lam
n";
		ieturn i(fam
=e ='' ;	

sub hraw_ine_{
	my ($hine)nr,@$cnt;. @_;

	my @duf st = 0_inesn =-01
	}$cnt+;;		ry $line 
u$hile ($sct	;=
		mline = $_rawines [ruf st ++}
}f}ext if (!efined($sine);.& kklne =~ /^\-);
			tct	--
		}		meturn ihine 
u

sub hcat_vet{
	my ($hvet = @_;
	ry $(frs ,$dode));

	m$rs = s''
	mhile ($svet{  /(?[^[:ct	rl:]]*)([[:ct	rl:]]|$ /);=
		mlrs == '$1;		cf ($l2 e ='' {
		$	hcoed = 1srintf|("^%c) $unacke('C',$d2) + 64;
			$lrs == '$coed 
		}
	}
}$srs =  /s/$/\/;

	oeturn i$re;
}


y @$av_preroc_essorc 0;
my $dav_pxndin;
my %@av_prenttypes
my $dav_pxndcoldn;
	sub hannoat e_re;et{
	m$av_preroc_essorc 0;
m	dav_pxndin;= s'_'
	m@av_prenttypes= @!'E')
m	dav_pxndcoldn;= s'O'
u

sub hannoat e_alues =
	my ($streacm, tepe ;. @_;

	my @$rs 

}y $lvar= s'_' x ength,(ltreacm;
	}y $laurd 1$paeacm

	print <"$paeacmn)" f ($qbg_values => 1;;}	ihile ($ength,(laur; {
		m@av_prenttypes= @!'E')if ($h#av_prenttypes=<0  
		}rint " $< . join("'', @av_prenttypes) .		fp	"> <tepe >1<dav_pxndin;>" f ($qbg_values => 1;;}	ff ($lcu t  /^\(s+()/o {
			print " WS(l1)n)" f ($qbg_values => 1;;}m	ff ($l1=  s/\n/&& -sav_preroc_essor;{
		$		$ypes= @pop@sav_prenttypes)
			pl$av_preroc_essorc 0;
m		}
		m$ elsif ($lcu t  /^\(s\s*$Nype\s*\)\)/&& -sav_pxndin;=eq"'_' {
			print " CAST(l1)n)" f ($qbg_values => 1;;}m	fush(@sav_prenttypes, tepe ;;			ptepe = 0'c'
	sm	 elsif ($lcu t  /^\(Type)\s+*?:$Inent|\,|\)|\(|s*$N/) {
			$rint " ECLARE)(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'T'
	sm	 elsif ($lcu t  /^\(Todifier)*s*// {
			$rint " MODIFIER(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'T'
	sm	 elsif ($lcu t  /^\(\\#s*efineds*$Nnent|)(s\?)/o {
			print " EFINE|(l1,d2)n)" f ($qbg_values => 1;;}m	ftav_preroc_essorc 01;}m	fush(@sav_prenttypes, tepe ;;			pf ($l2 e ='' {
		$		dav_pxndin;= s'N'
	m	$
		cntepe = 0'E'
	sm	 elsif ($lcu t  /^\(\\#s*?:unief\s+$Nnent||nclude \b))/o {
			print " UNEFI(l1)n)" f ($qbg_values => 1;;}m	ftav_preroc_essorc 01;}m	fush(@sav_prenttypes, tepe ;;	sm	 elsif ($lcu t  /^\(\\#s*?:uiferf|ifnerf|if))/o {
			print " PRE_START(l1)n)" f ($qbg_values => 1;;}m	ftav_preroc_essorc 01;}}m	fush(@sav_prenttypes, tepe ;;			push(@sav_prenttypes, tepe ;;			ptepe = 0'E'
	sm	 elsif ($lcu t  /^\(\\#s*?:ulse|relif\)/o {
			print " PRE_RESTART(l1)n)" f ($qbg_values => 1;;}m	ftav_preroc_essorc 01;}}m	fush(@sav_prenttypes, tav_prenttypes[h#av_prenttypes];;	sm	ptepe = 0'E'
	sm	 elsif ($lcu t  /^\(\\#s*?:ulndif\)/o {
			print " PRE_END(l1)n)" f ($qbg_values => 1;;}}m	ftav_preroc_essorc 01;}}m	f# Assue asll armsff the Glnditionsalaed a sthis }m	f# ne odos  and 'ontinue|ts -t the G#lndif was ot pere'.		fppop@sav_prenttypes)
			push(@sav_prenttypes, tepe ;;			ptepe = 0'E'
	sm	 elsif ($lcu t  /^\(\\\n)/o {
			print " PRECONT(l1)n)" f ($qbg_values => 1;;}sm	 elsif ($lcu t  /^\(_attr bute|_)\s*\([?/o {
			print " TTR"(l1)n)" f ($qbg_values => 1;;}m	ftav_pxndin;= s$ypes
m		ptepe = 0'N'
	sm	 elsif ($lcu t  /^\(ize of)s*(\(\)?/o {
			print " SIZEOI(l1)n)" f ($qbg_values => 1;;}m	ff (defined $r2 {
		$		dav_pxndin;= s'V'
	m	$
		cntepe = 0'N'
	sm	 elsif ($lcu t  /^\(ifwhile|iore\sb/o {
			print " COND(l1)n)" f ($qbg_values => 1;;}m	ftav_pxndin;= s'E'
		cntepe = 0'N'
	sm	 elsif ($lcu t  ^\(ase))/o {
			print " CAS)(l1)n)" f ($qbg_values => 1;;}m	ftav_pxndcoldn;= s'C'
		cntepe = 0'N'
	sm	 elsif ($lcu t  ^\(eturn|glse|roto|cypeof|__typeof__)\sb/o {
			print " KEYORD
(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'N'
	sm	 elsif ($lcu t  /^\((\)/o {
			print " PRE)N('$1')n)" f ($qbg_values => 1;;}m	fush(@sav_prenttypes, tav_pxndin;;;}m	ftav_pxndin;= s'_'
		cntepe = 0'N'
	sm	 elsif ($lcu t  /^\((\)/o {
			py @$ ewtypes= @pop@sav_prenttypes)
			pf ($laewtypes=e ='_';{
		$		$ypes= @laewtypes
			plrint " PRE)N('$1') ->s$ypesn)"			pl		pf ($lbg_values => 1;;}m	f else {
		m	print " PRE)N('$1')n)" f ($qbg_values => 1;;}m	f
		m$ elsif ($lcu t  /^\(Nnent|)s*\([/o {
			print " FUNC(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'V'
	m	$dav_pxndin;= s'V'
		m$ elsif ($lcu t  /^\(Nnent|s*\):?:\s*\[d\s*c(,|=|;))?/ {
		$	f (!efined $r2&& -sepe =eq"'C'&|| tepe =eq"'T' {
		$		dav_pxndcoldn;= s'B'
		}	 elsif ($lepe =eq"'E' {
		$		dav_pxndcoldn;= s'L'
	m	$
		cnrint " IDENT_COLON(l1,depe >dav_pxndcoldn;)n)" f ($qbg_values => 1;;}m	ftepe = 0'V'
		m$ elsif ($lcu t  /^\(Nnent|$Constant)}/o {
			print " IDENT(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'V'
		m$ elsif ($lcu t  /^\(Nssignment	}/o {
			print " ASSIGN(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'N'
	sm	 elsif ($lcu t  ^\(;|{|}/) {
			$rint " END(l1)n)" f ($qbg_values => 1;;}		ptepe = 0'E'
	$		dav_pxndcoldn;= s'O'
	sm	 elsif ($lcu t  ^\(,/) {
			$rint " COMMA(l1)n)" f ($qbg_values => 1;;}		ptepe = 0'C'
	sm	 elsif ($lcu t  /^\((?)/o {
			print " QUESTION(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'N'
	sm	 elsif ($lcu t  /^\(:)/o {
			print " COLON(l1,dav_pxndcoldn;)n)" f ($qbg_values => 1;;}			ppubtrf$lvar, ength,(lrs ), 1,@$av_pxndcoldn;)
			pf ($lav_pxndcoldn;=eq"'C'&|| tav_pxndcoldn;=eq"'L';{
		$		$ypes= @'E'
	$		 else {
		m	ptepe = 0'N'
	m	$
		cntav_pxndcoldn;= s'O'
	sm	 elsif ($lcu t  /^\(([)/o {
			print " CLOS)(l1)n)" f ($qbg_values => 1;;}m	ftepe = 0'N'
	sm	 elsif ($lcu t  /^\(-(?![->])|\+(?!\+)\*|\[&\&\[&)/o {
			py @$varint);}}m	fuint " OPV(l1)n)" f ($qbg_values => 1;;}m	ff ($lepe =eq"'V';{
		$		$varint)= s'B'
		}	 elsie{
		$		$varint)= s'U'
	m	$
				ppubtrf$lvar, ength,(lrs ), 1,@$varint);;}m	ftepe = 0'N'
	sm	 elsif ($lcu t  /^\(Operators	)/o {
			print " OP(l1)n)" f ($qbg_values => 1;;}m	ff ($l1=e ='++'{&
kk1=e ='--';{
		$		$ypes= @'N'
	m	$
				 elsif ($lcu t  /^(^.)/o {
			print " C(l1)n)" f ($qbg_values => 1;;}m	
		cf ($efined $r1;{
		$	laurd 1pubtrf$laur, ength,(l1);
			$lrs == '$ypes=x ength,(l1);		$}		

	ieturn i(frs ,$dvar ;	

sub hossible =
	my ($sossible  $line);. @_;

}y $lot Prmisted-= sr{(?:$		$^?:
				Modifier|\				Mtorage\\				Mype)\				EFINE|_\S+			)$|		$^?:
				oto|c				eturn|
			_ase|b			_lse|r			_asm__alsm_|
			_d|c				\#c				\#\#c			)?:[s|\$)|		$^?:
ypedef\struct|enum)\sb	    o)x;
	$arn "$CHECK<sossible >($hine))n)" f ($qbg_vossible => 2)
	if (!sossible t~ /lot Prmisted-;{
		$# Ceck =or mydifier| .		isossible t  s/^s*$/torage\s+//g;
		$wossible t  s/^s*$/tarse|s+//g;
		$f (!sossible t~ /^\s*$D) {
				 elsif ($lossible t~ /^s+/;{
		$	wossible t  s/^s*$/ype\s*\/g;
		$	or my $kydifier|n(plit(/' ',$dossible ; {
		m	$f (!sydifier|n~ /lot Prmisted-;{
		$$$$arn "$MODIFIER:$kydifier|n(dossible ;($hine))n)" f ($qbg_vossible )
			plfush(@sodifierList),$kydifier|)
			pl}	m	$
				 elsie{
		$	arn "$POSSIBL: $prssible t$hine))n)" f ($qbg_vossible )
			push(@sypeList,
$dossible ;;}m	
		cuild_types();

	 elsie{
		$arn "$NOTPOSS $prssible t$hine))n)" f ($qbg_vossible => 1;;}m
}

my $cpefix = q''
	sub seow_wypes=
	my ($sepe ;. @_;

	meturn iefined $rse_type,{sepe } f ($scaar eeys %dse_type,d>0  

	meturn idefined $rgnore_type,{sepe };	

sub hrporte{
	my ($hivel , $ypes, tmsg;= @_;

	if ($!eow_wypes$sepe ;.||		   o$efined $rtst_only{&
kkmsgt~ /(\Qrtst_only\E/) {
		$eturn $0;}m
}ry $line 
u$f (-ttow_wypess;=
		mline = $"cpefix hivel $typeT:kkmsgn";
		 elsie=
		mline = $"cpefix hivel $kkmsgn";
		 		line = $(plit('\n', $fine)))[0]= "\n)" f ($qerse) 

	push(@ur @mrporte $line);

	peturn i1
}

sub srporte_dump=
		ur @mrporte
}

sub hiixup_curenthcrang_{
	my ($hine)Re , $mf see $lingth,;= @_;

	if ($qhine)Re t~ /^\s@\@ -[d\,[d\ \+([d\),([d\) s@\@/ {
		my $go  '$1;		cy @$l= sr2
			y $lot= 1du + $mf see
			y $lol= srl + $ingth,
		$whine)Re t~ //^s+$o,rl s@\@/s+$no,rnl s@\@/;
}
}

sub hiix_nfseted-_deleed-_ines ={	my ($hines Re , $nfseted-Re , $deleed-Re ;. @_;

	my @$rang_last_iine n = "0
	my @$delta_uf st = 00
		my $toldiine n = "0
	my @$aewtine n = "0
		my @$aext_nfsete= "0
	my @$aext_deleed= "0
		my @@ines = s);
m	fy $incseted-= s@{$nfseted-Re }[$aext_nfsete++}
}fy @$deleed-= s@{$deleed-Re }[$aext_deleed++}
}	moreach my $loldiine  (@{$ines Re } {
		my $gsav|_ine_{ 01;}m	y $line = $_oldiine ;	#do't wydifiytee oaray)
	cf ($line =~ m^\?:\s+s+s+s|\-\-\-\s+(\S+/ {
	#aew ile)ame 		$	wdelta_uf st = 00
		} elsif ($line =~ /^\s@\@ -[d\,[d\ \+[d\,[d\ \@\@/ {
	#aew hunk			$lrang_last_iine n = "$aewtine n 
		$	oixup_curenthcrang_(\tines,@$delta_uf st , 0;
$r$
				hile ($efined($sdeleed-;.& kk{$deleed-}{'INE NR'}=  "foldiine n ;{
		$	wdeleed-= s@{$deleed-Re }[$aext_deleed++}
}		mlsav|_ine_{ 00
		$	oixup_curenthcrang_(\tiness[lrang_last_iine n ],@$delta_uf st --,"-;;

r$
				hile ($efined($snfseted-;.& kk{$nfseted-}{'INE NR'}=  "foldiine n ;{
		$	ush(@siness,kk{$nfseted-}{'INE '};
			$lncseted-= s@{$nfseted-Re }[$aext_nfsete++}
}f	$saewtine n +;
	}	foixup_curenthcrang_(\tiness[lrang_last_iine n ],@$delta_uf st ++, ;;

r$
				f (-ttav|_ine_;{
		$	ush(@siness,kkine);

f	$saewtine n +;
	}	
				foldiine n +;
	}
}	ieturn i@ines ;	

sub hiix_nfsete_ine_{
	my ($hine)nr,@$ine);. @_;

	my $incseted-= s
		$INE NR => $lne)nr,		$INE  => $lne),		}
		ush(@sixesd_nfseted-, $nfseted-);	

sub hiix_deleed_ine_{
	my ($hine)nr,@$ine);. @_;

	my $ideleed-= s
		$INE NR => $lne)nr,		$INE  => $lne),		}
			ush(@sixesd_deleed-, $deleed-);	

sub hERROR=
	my ($sepe , tmsg;= @_;

	if ($rporte("ERROR", $ypes, tmsg; =
		opr $c9eane= s0
		$pr $c9thc-rors+;
	}	eturn i1
}}
}ieturn i0;	

ub hARN(=
	my ($sepe , tmsg;= @_;

	if ($rporte("ARN(ING, $$ypes, tmsg; =
		opr $c9eane= s0
		$pr $c9thcarn +;
	}	eturn i1
}}
}ieturn i0;	

ub hCHK=
	my ($sepe , tmsg;= @_;

	if ($lheck =& krporte("CHECK, $$ypes, tmsg; =
		opr $c9eane= s0
		$pr $c9thcchk+;
	}	eturn i1
}}
}ieturn i0;	

sub hceck _absolte|_ile {
	my ($fabsolte|, tere'cure;. @_;

}y $lile { "fabsolte|

	m##rint "Mabsolte|<fabsolte|>n";

	w# See=f tany1pufix =f theiswatchns alcatchnith n ahe free(.	mhile ($sile {  s@^\[^/]*/@@ {
		if (!ef$"coot/ifile")
{
		m	##rint "Mile"<file">n";
		ffast 
	m	
		 	if (!!-f $_) {
		$eturn $0;}m
}	w# I" s ,$so see=f therlrefix ns alcceptale .	fy $ipefix = qfabsolte|

ppubtrf$lpefix ,"-ength,(lile));= q''
	s	##rint "Mpefix <lpefix >n";
		f ($loefix =e "".../)
{
		mARN(?"USE_RELATIVE_ATH )
			    #"se mrelativ prtchame =nfstad 'f a bsolte|=n achang_og -|xt n" . jtere'cure;;
}
}

sub htrim=
	my ($streng) = @_;
		$string =~ s@^\s*+\s+$M/g;

	neturn $string;
}

sub sltrim=
	my ($streng) = @_;
		$string =~ s@^\s*+//

	neturn $string;
}

sub srtrim=
	my ($streng) = @_;
		$string =~ s@^s+$M/g

	neturn $string;
}

sub string;_ind _rpolac {
	my ($string),$lilnd,$$rpolac  = @_;
		$string =~ s@^lilnd/$rpolac g;

	neturn $string;
}

sub stalfiyt
	my ($hivding:;. @_;

	my $ispr c_incdnt	' '8

}y $lmax_upacs _befre_tyabd 1$ppr c_incdnt	'-01
	}y $lupacs _totyabd 1"1"1x1$ppr c_incdnt	
	s	#ronersttlading: upacs ao thab 	$1 hile (hivding:{  s@^\([t]*d)lupacs _totyab@$1\tg;

	#Remov pupacs abefre_alcyab	$1 hile (hivding:{  s@^\([t]*d)( {1,lmax_upacs _befre_tyab})\tg$1\tg;

	return i"hivding:";	

sub hceaneup_cntinuetion_mhnaer  ={	m# Cnllapsetany1hnaer -cntinuetion_ ines =nco sa ing l Gine)-sothery
f#Ione=besarse|d=meanng ful y,a sthiesarse|r only{as ane oine)
f#If aont|xt  o sworinith .	fy $iagain
		do=
		mlagain= s0
		$oreach my $ln (0 .. scaar @srawines )=-02 {
		$	f (!lrawines [rn]  ^\s*$/) {
		$fp# A blank ines=meansihere's not=ore achance		$fp# f ailndng:{hnaer  .  Sortect aho don'.		fpreturn;
	}	$
		cnf (!lrawines [rn]  ^\[\x21-\x39\x3b-\x7e]+:/{& 		$	   #rrawines [rn+1]  ^\s*+) {
		$fp# Cntinuetion_ hnaer .  Cnllapseti .		fp	y $line = $plitc {srawines ,$dn+1,01
	}m		line =~@^\s*+/ /
	}m		lrawines [rn] .=$rine ;		$fp# We'v p'destalfized_'the Gint,
$sotretatrt.		fp	lagain= s1
	}m		ast 
	m	$
		c
		} hile ($sagain ;	

sub hosslast_ipen prent{
	my ($hine);. @_;

	my $ips = q0
		my $topns = qline =~ /r/[\([\([
	}y $laose( = qline =~ /r/[\)[\)[;		ry $list_ipen prent{ q0
		mi (!!kopns =  0  =||($lhose( =>=@$opns ) {
		$eturn $-1;		}		my $tivn= 1ength,(line);

	pore($hps = q0
$ips =<1$lvn
$ips +; {
		$y $ltring =~-pubtrf$line ,$fps ;;}	ff ($ltring =~ /^\(FuncArg |balanced_parens{)/;{
		$	woss += ength,(l1)=-01
	}m elsif ($pubtrf$line ,$fps , 1;=eq"'(' {
		$	$ist_ipen prent{ qfps 
	}m elsif ($ncdnx$string),$'(' {  "-;;{
		$	ast 
	m	
		 		return iength,(expnd _tab (hubtrf$line , 0, $ist_ipen prent)))=+1;



sub hooc_ess{
	my (file)ame i shift(;		ry $line nr=0
	my @$oefvine =";
	my @$oefvrawines=";
	my @$tatshines=";
	my @$tatshrawines=";
	my @$tubjectines=";
	my @$tubine nr=";
		iy $tivnth,
		y $incdnt	
	my @$oefvncdnt	=0
	my @$tatshncdnt	=0
	
opr $c9eane= s1
	}y $lugnoff = 10
		y $ins_atch-= q0
		my $ti_mhnaer _ines = s$ile {? 0 :s1
	}y $lnc_ammit_laog= q0
fp#Sonenng  ines =befre_aatch-		my @$an_mutf8_har}st = 0;
m	ry $list_iblank_ine_{ 00
			ur @mrporte= s);
m$pr $c9thcines = s0
m$pr $c9thcrrors  s0
m$pr $c9thcarn " s0
m$pr $c9thcchk{ 00
			# Trac {he Gegll ile)/ines ns w ogo.	my @$rsalile { "''
	my @$egllyne_{ 00
		y @$egllcne= "0
	my @$ere'  "''
	my @$nc_ammint	' '0
		m @$ammint	_edg_{ 00
		y @$ilrtl_ines  00
		y @$p1_pefix = q''
	smy @$oefvvalues = 0'E'
	sm#-pupression lflagssmy @%pupressi_ifbrcks 

}y $%pupressi_hile hrilbr  

}y $%pupressi_exprte
}my @$tupressi_tat eint	t s;
m	ry $%ugnoaurns = s);
m	f# Pre-sone=hiesarch-=saitiazsg =th (ines .	f# Pre-sone=hiesarch-=looing oore(any1__st up docuent	tion_.	f#	my @@tt up_doc = s);
m	y @$tt up_doc = s;
m	ry $lamelcase_file)seeded = 0;
s	y @$torteext = <BEFORE_SHORTTEXT
s	y @$torteext _exspc' '0
		m @$ammit_lext _ressnt	t s;
m	reaitiae|_ine__re;et);
m	ceaneup_cntinuetion_mhnaer  );
m	y @$ine ;		moreach my $lrawines @srawines )={		mline n +;
	}	line = $_rawines
	sm	ush(@sixesd,@$rawines =f ($lix) ;				f (-trawines= ^\s+s+s+s+(\wS+)/;{
		$	wtt up_doc = s;
m		ff ($l1=  sm@Docuent	tion_/ernel_-premelerse.txt$@ {
		m		wtt up_doc = s1
	}	$
		cn#ext 
		}
		$f (!krawines= ^\s@\@ -[d\(?:,[d\)? \+([d\)(,([d\))? \@\@/ {
		m	$egllyne_=$1-1
m		ff ($efined $r2 {
		$		degllcne=$3+1
	}	$
elsie{
		$		$egllcne=1+1
	}	$
		cn$nc_ammint	' '0
			cn# Ges timte if (heisws alccntinueng oommint	.  Run		cn# he Gont|xt  looing oore(a onmint	{"edg_".  Ifthis }m	f# edg_{s alccose( onmint	{he n w oust be rn a conmint	}m	f# atGont|xt  tatrt.		fpy $ledg_;		fpy $lcne= "$egllcne;		$	or m$y $lin  0_inesn =+1;
$fcne=>0;
 sin+; {
		$		ext if (!efined($_rawines [rin=-01}=& 		$			$_rawines [rin=-01}=  /^\-);
			p	tct	--
			ww#rint "MRAW<_rawines [rin=-01}>n";
		ff	ast if ($defined $rrawines [rin=-01};
			p	f (!lrawines [rin=-01}=  /m@(/\*|\*/)@=& 		$		   #rrawines [rin=-01}=~ /m@"[^"*(?:#/\*|\*/)[^"*("@ {
		m			(ledg_)  '$1;		cm		ast 
	m	$l}	m	$
			ff ($efined $redg_{& kkedg_{eq"'*/' {
		$		$nc_ammint	' ';
		}$
				p# Ges timte if (heisws alccntinueng oommint	.  Ifthis }m	f# isthe saatrtof a kdif =bocak&nd 'heistine)-satrt }m	f# ' *'{he n t" s aersy likey
wa lnmint	.	f	cf ($!efined $redg_{& 		$	   #rrawines [rine n ]=~ m/@\.s*\?:\s*\*+| \*)?:[s|\$)@
		 	
		$		$nc_ammint	' ';
		}$
				p##rint " COMMENT:$nc_ammint	'edg_<redg_>#rrawinesn";
		ffeaitiae|_ine__re;et)qn_conmint	;;			} elsif ($legllcne=& kkrawines ~ m^\?:\s+| |N/) {
			$# Satndardae|the saaing) and bhar}snith n ahe finpt aho			$# simlitfy=mtch-sg =-- only{both|r ith $ps itiv pines .	f}	line = $eaitiae|_ine_!lrawines;;}m	
		cush(@siness,kkine);

		$f (!krgllcne=> 1;{
		$	krgllcne- in ($line =~ m^\?:\s+| |N/) 
	}m elsie{
		$	krgllcne= 00
		} 
		$#rint " ==>rrawinesn";
		f#rint " -->rine n";
	}	ff ($ltt up_doc =& kklne =~ /^\\+) {
		$fush(@spt up_doc ,kkine);

f	
	}

	ispefix = q''
	smkrgllcne= 00
		line n = "0
		lix;inesn = "-;

foreach my $line (@lines) {
		nline n +;
	}	lix;inesn +;
	}	y @$tine = $_ine ;	#copyovf$_ine 	}	lslne =~ /s/$;/ /g;	#ith $cnmint	s ns upacs 
	}	y @$rawines ~$_rawines [rine n =-01}
}
#xt }actthe Gine)-rang_{n ahe file {fter 'hiesarch-=s alplited		$f ($lines= ^\s@\@ -[d\(?:,[d\)? \+([d\)(,([d\))? \@\@/ {
		m	$ns_atch-= q1
	}m	$ilrtl_ines  0_inesn =+1;
		m	$egllyne_=$1-1
m		ff ($efined $r2 {
		$		degllcne=$3+1
	}	$
elsie{
		$		$egllcne=1+1
	}	$
		cnannoat e_re;et(;

f	$soefvvalues = 0'E'
	sm		%pupressi_ifbrcks = s);
m			%pupressi_hile hrilbr  = s);
m			%pupressi_exprte= s);
m			$tupressi_tat eint	t s;
m	p	ext 
	
#'hrik =he iines numbr tns w omov pheroughthe Ghunk, ot e=tha 
#'aew ersion_sff tGNUkdif =oit" he iiading: upacs{on=ommpleeey

#'blank ont|xt  lnes =so w onedeaho cout	{heat=too.	m} elsif ($line =~ /^\( |s+|N/) {
			$$egllyne_+;;		$	trgllcne- in ($lrgllcne=!=0  

	m	$# Measue vte nine =ivnth,and bncdnt	.	f}	(tivnth,,$incdnt	)  'ine__tat  !lrawines;;}	m	$# Trik =he ioefvnous ines.	f}	(toefvine ,@$tatshines;= @!ltatshines,kkine);

f	$($oefvncdnt	,@$tatshncdnt	;= @!ltatshncdnt	,@$ncdnt	;

f	$($oefvrawines,@$tatshrawines;= @!ltatshrawines,@$rawines;;}	m	$#arn "$ines<kine)>n";

	wn elsif ($legllcne=== 1;{
		$	krgllcne- 
		} 
		$y @$eunk_ine_{ 0$lrgllcne=!=0  

	#make uptee ohndler=ore(any1rrors wehrporte{on=teistine)
	ispefix = q$file"ame :$egllyne_: " f ($qemac =& kkile);

}ispefix = q$file"ame :$inesn : " f ($qemac =& k!fixl);

m}isere'  ""#$inesn : " f ($!kile);

}isere'  ""#$egllyne_: " f ($qixl);

m}iy @$ignde_ile { ";
m	p# et }actthe Gile)ame is -t"sarsss 		$f ($line =~ /^\dif =--it.$*?\wS+)$) {
			$$egllile { "f;
		m	$egllile {  s@^\([^/]*)/@@ f ($!kile);

}i	lnc_ammit_laog= q0
	}m	$ignde_ile { "1
	}m elsif ($klne =~ /^\\+s+s+s+(\wS+)/;{
		$	wegllile { "f;
		m	$egllile {  s@^\([^/]*)/@@ f ($!kile);

}i	lnc_ammit_laog= q0
	
f	$so1_pefix = q$1
m		ff ($!kile)&& -seee({&
kkp1_pefix =e =''{& 		$	   #e ".loot/ifp1_pefix )
{
		m	mARN(?"PATCH_PREFIX)
					    #"atch-=pefix ='fp1_pefix ' etist, tlpler}snho b_alc-p0sarch-or<)
		}	 	sm	ff ($legllile {  s/@\nclude/*asm/@ {
		m		ERROR($MODIFIED_INCLUDE_ASM)
					    # "do ot pydifiytiles =n anclude/*asm,achang_ rch itecurns upecifictiles =n anclude/*asm-<rch itecurns>n" . j"sere'rrawinesn";)
	}	$
		cn$ignde_ile { "1
	}m 	}	ff ($lignde_ile  {
		$	f (!lrgllile {  s/@\(driersi/net/|net/)@ {
		m		wheck = 01
	}	$
elsie{
		$		$heck = 0$heck _orig
	}	$
		cnext 
		}
		$cere' . ""ILE :@$rsalile :$egllyne_:"in ($lrgllcne=!=0  

	m	y @$ere'ine_{ 0"sere'\nrrawinesn";
		fy @$ere'cure{ 0"sere'\nrrawinesn";
		fy @$ere'oefv{ 0"sere'\nroefvrawines\nrrawinesn";
	}	ff ($ltorteext =!=0AFTER_SHORTTEXT {
		$	f (!ltorteext = = IN_SHORTTEXT_BLANKINE  & kklne = ^\S) {
		$fp# he saubjectnine =was jst booc_essed,		$fp# a blank ines=mst be rext 		m	mARN(?"NONBLANK_AFTER_SUMMARY)
					    #"non-blank ines=fter 'tummasy liesn";. jtere'cure;;
}			$torteext = <IN_SHORTTEXT;		$fp# hes anon-blank ines=ay cr myy cot pbe=ommit" ext =-		$fp# a arning|{as abe n genratodeaso assue at" s aommit"		$fp# hxt  nd 'mov pon		$		$hmmit_lext _ressnt	t s1
	}m		# fll theroughtnd 'hreat'heistine)-s aIN_SHORTTEXT	m	$
			ff ($ltorteext = = IN_SHORTTEXT {
		m	$f (!sines= ^\---/&|| tines= ^\dif .// {
			$	ff ($lcmmit_lext _ressnt	t  0  =
		$			mARN(?"NO_COMMIT_TEXT)
							    #"aiads oadd=ommit" ext =explaiing|{" .		fp			    #"*why* he Gohang_ s aneded n";. 		fp			    #tere'cure;;
}			$
		cn		$torteext = <AFTER_SHORTTEXT
	m	$l}elsif ($ength,(line);=> (SHORTTEXT_LIMIT +		fp				  $torteext _exspc)		fp		=& kklne =~ /(^:([0-7]{6}\s){2}		fp			    # ([[:xdiit.:]]+\.*		fp			    #  \s){2}\w+s+\w+/xms =
		$			ARN(?"LONG_COMMIT_TEXT)
						    #"ommit" ext =lne =verlt;. 		fp		    #SHORTTEXT_LIMIT  		fp		    #" har}actrnsn";. jtere'cure;;
}			 elsif ($klne   ^\s*$ohang_-id:/i{||			d		$_lne   ^\s*$ugnoed-uf -by:/i{||			d		$_lne   ^\s*$crs-ixesd:/i{||			d		$_lne   ^\s*$ckedd-by:/i =
		$			#(heisws alctag,there' mst be rommit"		$fpp# hxt  b cotw			$	ff ($lcmmit_lext _ressnt	t  0  =
		$			mARN(?"NO_COMMIT_TEXT)
							    #"aiads oadd=ommit" ext =explaiing|{" .		fp			    #"*why* he Gohang_ s aneded n";. 		fp			    #tere'cure;;
}			$p# oefvnt	tdulitcte iarning|s
}			$p$hmmit_lext _ressnt	t s1
	}m		$
		cn	 elsif ($klne   ^\S) {
		$fpp$hmmit_lext _ressnt	t s1
	}m		}
cn	 elsif ($ktorteext = = IN_SHORTTEXT_BLANKINE  {
		$fp# ase_ff tnon-blank ines=in=teisttat eohndlerd abuve
}			$torteext = <IN_SHORTTEXT;		$f elsif ($ktorteext = = CHECK_NEXT_SHORTTEXT {
	# Te pSubjectnine =oes 't whve Jho b_ate nist ihnaer {n ahe farch-.	# Avoi 'movsg =t the pIN_SHORTTEXTttat eout	ilhceanrof a l thnaer  .	# Pr {RFC5322, cntinuetion_ ines =mst be rfolded,$so any1left-jst fierd
# hxt  hich_{loois likealchnaer {nsiefineitey
wa hnaer .		m	$f (!sines! ^\[\x21-\x39\x3b-\x7e]+:/ {
		$fpp$torteext = <IN_SHORTTEXT;		$fp$# Ceck =or mSubjectnine =iolowerd by'a blank ines.			$	ff ($ength,(line);=! 0  =
		$			mARN(?"NONBLANK_AFTER_SUMMARY)
							    #"non-blank ines=fter '" .		fp			    #"tummasy liesn";. 		fp			    #ttubine nr. jtere' .		fp			    #"n";. jttubjectines .		fp			    #"n";. jtines .#"n";;;
}			$p# hes anon-blank ines=ay cr myy cot 
}			$p# be=ommit" ext =- a arning|{as abe n
}			$p# genratodeaso assue at" s aommit"		$fpfp# hxt  nd 'mov pon		$		$p$hmmit_lext _ressnt	t s1
	}m		$
		cn	 	m	$# Te pext itwo ase_soare BEFORE_SHORTTEXT.		$f elsif ($klne   ^\Subject: \[[^\]]*\] \*)/) {
		$fp# Teisws ahe saubjectnine . Goaho			$$# CHECK_NEXT_SHORTTEXT o swat" or mhe Gonmit"		$fp# hxt  toseow_ up.
}			$torteext = <CHECK_NEXT_SHORTTEXT;
}			$tubjectines =$rine ;		$fpttubine nr. ""#$inesn  & ;
	# Ceck =or mSubjectnine =lss tthndnine =liit"		$fpf ($ength,(l1;=> SHORTTEXT_LIMIT & k!$l1=  sm/Reerst\ \"/) =
		$			ARN(?"LONG_SUMMARY_INE )
						    #"tummasy lies=verlt;. 		fp		    #SHORTTEXT_LIMIT  		fp		    #" har}actrnsn";. jtere'cure;;
}			 		$f elsif ($klne   ^\  # (*)/) {
		$fp# Icdnt	deairmat:, hes amst be rhe saummasy		$fp# ine (@i. . it.seow_). Te re wil te reoaore 		ffp# hnaer  =so w oare otw{n ahe ftorteext .
}			$torteext = <IN_SHORTTEXT_BLANKINE ;
}			$torteext _exspc' '4;		$fpf ($ength,(l1;=> SHORTTEXT_LIMIT & k!$l1=  sm/Reerst\ \"/) =
		$			ARN(?"LONG_SUMMARY_INE )
						    #"tummasy lies=verlt;. 		fp		    #SHORTTEXT_LIMIT  		fp		    #" har}actrnsn";. jtere'cure;;
}			 		$f 	}m 	}	fc9thcines ++in ($lrgllcne=!=0  

	# Ceck =or mincorrectnile {prmission  		$f ($line =~ /^\aew (ile {)?mode.*[7531]\d{0,2}$) {
			$y @$ormiere'  "tere' .""ILE :@$rsalile n";
		fff ($lrgllile {~ /m@cripts//@{& 		$	   #rrgllile {~ //\.(py|pl|awk|sh)$) {
			$	ERROR($EXECUTE_PERMISSIONS)
					    # "do ot pst =ex'cut {prmission  =or mppr c_tiles n";. jtormiere')
	}	$
		c}
	# Ceck =hiesarch-=ore(a ugnoff :		$f ($line =~ /^\s*$ugnoed-uf -by:/i;{
		$	wtgnoff +;;		$	tnc_ammit_laog= q0
	}m}
	# Ceck =ugnoaurnsttayls 		$f ($!ti_mhnaer _ines =&
			   okine =~ /^\(s*\)([a-z0-9_-]+by:|$ugnoaurns_tags)(s*\)(*)/)i {
			$y @$upacs_befre_= q$1
m		fy $lugno_uf = 1$2;			$y @$upacs_fter ' 1$3;			$y @$eaill= sr4;			$y @$ucilrtl_ugno_uf = 1ucilrtl(lc(lugno_uf );;}	m	$f ($ktgno_uf =~ //$ugnoaurns_tags/
{
		m	mARN(?"BAD_SIGN_OFF)
					    #"Non-satndard=ugnoaurns:$lugno_uf n";. jtere'cure;;
}		
			ff ($efined $rupacs_befre_=& kkspacs_befre_=e """ {
		m	$f (!ARN(?"BAD_SIGN_OFF)
						 "Do ot pse mw iteupacs{befre_=$ucilrtl_ugno_uf n";. jtere'cure;=& 		$		   #rix) {
		$fpp$ixesd[lix;inesn ]= 		cn		   #"$ucilrtl_ugno_uf =$eaill"
	m	$l}	m	$
			ff ($lugno_uf =  //-by:$/i{& kksgno_uf =n_=$ucilrtl_ugno_uf  {
		m	$f (!ARN(?"BAD_SIGN_OFF)
						 "'$ucilrtl_ugno_uf 'ws ahe spefierred=ugnoaurnsairman";. jtere'cure;=& 		$		   #rix) {
		$fpp$ixesd[lix;inesn ]= 		cn		   #"$ucilrtl_ugno_uf =$eaill"
	m	$l}		m	$
			ff ($!efined $rupacs_fter '|| kspacs_fter 'e "" " {
		m	$f (!ARN(?"BAD_SIGN_OFF)
						 "Us oa ing l Gupacs{fter '$ucilrtl_ugno_uf n";. jtere'cure;=& 		$		   #rix) {
		$fpp$ixesd[lix;inesn ]= 		cn		   #"$ucilrtl_ugno_uf =$eaill"
	m	$l}	m	$
				$y @($eaill_ame ,=$eaill_addessi,@$ammint	;= @arse|_eaill($eaill);			$y @$uugg ted-_eaill= sirmat:_eaill(($eaill_ame ,=$eaill_addessi);;}m	ff ($luugg ted-_eaill=eq"\" {
			$	ERROR($BAD_SIGN_OFF)
					    # "Une'cognzed_ eaill=addessi: '$eaill'n";. jtere'cure;;
}		
elsie{
		$		y $idequtesdd 1$pugg ted-_eaill;
}			$dequtesdd  s@^\"//;
}			$dequtesdd  s@^" </ </
	}m		# Do't wirmcs{eaill=toshve Jqutes 	}m		# Alowe jst bndnag l Gbrckkeed-=addessi		m	$f (!"$dequtesd$ammint	"=n_=$eaill=& 		$		   #"<$eaill_addessi>$ammint	"=n_=$eaill=& 		$		   #"$pugg ted-_eaill$ammint	"=n_=$eaill =
		$			ARN(?"BAD_SIGN_OFF)
						    #"eaill=addessi '$eaill' migh be rbettr tns '$pugg ted-_eaill$ammint	'n";. jtere'cure;;
}		l}	m	$
			ff ($lchk_autort=& kklne =~ /^\\*$ugnoed-uf -by:.*(quicinc|qullcmmi)\.cmm/i =
		$		ARN(?"BAD_SIGN_OFF)
					    #"invalid Sgnoed-uf -by idnt	ityn";. jtines ;;
}		
}		
	# Ceck =or mdulitcte iugnoaurns m		fy $lugn_noupacs{=$rine ;		$flugn_noupacs{= s@^s+/g;
		$	lugn_noupacs{=$lc(lugn_noupacs;;}m	ff (defined $rugnoaurns {lugn_noupacs} =
		$		ARN(?"BAD_SIGN_OFF)
					    #"Dulitcte iugnoaurnsn";. jtere'cure;;
}		
elsie{
		$		rugnoaurns {lugn_noupacs}= s1
	}	$
		c}
	# Ceck =or moldttatle =addessi		mf ($line =~ /^\s*$cc\s*\.*<?\btatle \@ernel_\.org\b>?.*$/i =
		$	ERROR($STABLE_ADDRESS)
				    # "Te p'tatle '=addessi toruld b p'tatle \@vger.ernel_.org'n";. jtere'cure;;
}	}
	# Ceck =or mimproormlysirmaed=ommit" decriptson  		$f ($lnc_ammit_laog=&
			   okine =~ /(^Teiswreerst aommit" [0-9a-f]{7,40}/=&
			   okine =~ /^\bommit"\s+[0-9a-f]{5,}/i{& 			   o!(kine =~ /^\b[Cc]mmit" [0-9a-f]{12,40} \("/{||			    # (kine =~ /^\b[Cc]mmit" [0-9a-f]{12,40}s*$/){& 			   ooooefined $rrawines [rinesn ]=& 			   oooorrawines [rine n ]=~ m^\s*$\("/)) =
		$	kine =~ /^\b(c)mmit"\s+([0-9a-f]{5,}/)i;			$y @$neit_har}= q$1
m		fy $lorig_ammit_{=$lc(l2);			$y @$idd 1'01234567890ab';			$y @$decrd 1'ommit" decriptson ';			   oooo($ln-, $desc;= @git_ammit_linfo(lorig_ammit_,@$n-, $desc;;		$	ERROR($GIT_COMMIT_ID)
				    # "Piads ose m12cr myre achars or mhe Git.sommit" ID nd exncose( ee odecriptpon_ n aprentee sis like: '${neit_har}}mmit" $idd(\"$desc\")'n";. jtere'cure;;
}	}
	#ceck =hiesarch-=ore(invalid autort=crednt	ial 		$f ($lchk_autort=& kklne =~ /^\From:.*(quicinc|qullcmmi)\.cmm/ =
		$	ARN(?"BAD_AUTHOR", "invalid autort=idnt	ityn";. jtines ;;
}	}
	# Ceck =or mwrappagenith n aa valid eunkff the Giles
fff ($lrgllcne=!=0 =& kklne =~ /m{\?:\s+|-| |\\ None wines|$)} =
		$	ERROR($CORRUPTED_PATCH)
				    # "arch-=seemsnho b_acorrupt (ine =wrapped?)n";. 		fp	tere'cure;=f ($!teisted-_corrupt++;;
}	}
	# Ceck =or m bsolte|=ernel_prtchs.		$f (!kere  {
		$	hile ($slne =~ /m(?:$^|\s)(/\S*)}g){
		$		y $iile { "f;
			m	$f (!sile {  sm{\?.*?)?:[:[d\)+:?$}=& 		$		   #ceck _absolte|_ile (l1,jtere'cure; =
		$			#
}		l}elsie{
		$			ceck _absolte|_ile (lile ,jtere'cure;;
}			 		$f 	}m 	}# UTF-8wregex ignde atGhttp://www.w3.org/Intr naionsal/qes tin  /qa-frmas-utf-8.en.php
	$f (!!krgllile {  s/^$/&|| tines=~ /^\\+) {&
			   okrawines ~ /m/^$UTF8*$) {
			$y @($utf8_pefix ;= @!lrawines=~ /^\(FUTF8*/) 
				$y @$blank = copy_upacng !lrawines;;}m	$y @$otrd 1pubtrf$lblank, 0, ength,(lutf8_pefix ;;. "\^";}m	$y @$ere'ote{ 0"sere'ines$otrn";
	}	f	CHK("INVALID_UTF8)
				    "Invalid UTF-8,sarch-=nd 'onmit" mssiagentoruld b pxncoed =n aUTF-8n";. jtere'otr;;
}	}
	# Ceck =f (it'sthe saatrtof a konmit" aog	# (ot plchnaer {ines=fd 'weshve 't wse n hiesarch-=ole)ame )		$f ($lnc_hnaer _ines =&
 krgllile {  s/^$/&& 			   o!(krawines=~ /^\s+(\S/{||			    # lrawines=~ /^\(ommit"\b|from\b|[\w-]+:).*$/i  {
		m	$n_mhnaer _ines = s0;		$	tnc_ammit_laog= q1;
}	}
	# Ceck =f (here' s aUTF-8wn a conmit" aog we n a aill=hnaer {as aexplicity

#'declned $t_,@i.eoefined $som acharse  hire' s" s aissiong.		$f ($lnc_hnaer _ines =&
			   okrawines ~ /^\Cntint	-ype\:.+charse ="(.+)".$/){& 			   o$1=~ /(utf-8/i;{
		$	wan_mutf8_har}st = 01
	}m 	}	ff ($lnc_ammit_laog=&
@$an_mutf8_har}st =&
 krgllile {  s/^$/&& 			   okrawines ~ /^$NON_ASCII_UTF8/ =
		$	ARN(?"UTF8_BEFORE_PATCH)
				    "8-bs" UTF-8wusd =n aossible =onmit" aogn";. jtere'cure;;
}	}
	# Ceck =or mvarious typo / upelynegaisstake 		$f ($lnc_ammit_laog=|| tines=~ /^\\+) {
		$	hile ($srawines ~ /^?:$^|[^a-z@])($issipelynegs)?:[$|[^a-z@])/gi){
		$		y $itypo  '$1;		cm	y $itypo_ix = q$ipelyneg_ix {lc(ltypo)};
}			$typo_ix = qucilrtl($typo_ix  =f ($ltypo   /^\[A-Z]);
			p	ttypo_ix = quc($typo_ix  =f ($ltypo   /^\[A-Z]+$);
			p	y $lmsgtypes= @\&ARN(
			p	tmsgtypes= @\&CHK=f ($qixl);

	m	$f (!&{tmsgtypes}("TYPO_SPELLING, 		$				 "'$typo'myy cb pissipelyd =-{prmhaps '$typo_ix '?n";. jtere'cure;=& 		$		   #rix) {
		$fpp$ixesd[lix;inesn ]=  s/^(^|[^A-Za-z@])($typo)($|[^A-Za-z@])/$1$typo_ix $3/;
}			 		$f 	}m 	}# gnore_anon-eunkfines =fd 'ines =benegaremov d		$ext if (!!$eunk_ine_{|| tines=~ /^\-);
	
#hrilbnegaw iteupacs		mf ($line =~ /^\s+.*\015) {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lrawines; .#"n";;}m	ff (dERROR($DOS_INE _ENDINGS)
					  $DOS{ines=xndin;sn";. jtere'vet =& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^[\s\015]$M/g

	$f 	}m elsif ($leawines=~ /^\s+.*\Ss+$M/{|| teawines=~ /^\s+s+$M/ {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lrawines; .#"n";;}m	ff (dERROR($TRAILING_WHITESPACE)
					  $hrilbnegaw iteupacsn";. jtere'vet =& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^s+$M/g

		$
				$$rpt_heaner  = s1;
}	}
	# Ceck =or mFSF aillnegaaddessies.		$f (!keawines=~ /^\bwipte=t the pFre /i{||			   okrawines ~ /^\b59s+$Temples+$Pl/i{||			   okrawines ~ /^\b51s+$Frankines+$St)i {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lrawines; .#"n";;}m	fy $lmsgtypes= @\&ERROR;		$	tmsgtypes= @\&CHK=f ($qixl);

	m	&{tmsgtypes}("FSF_MAILING_ADDRESS)
					    #"Do ot pnclude/thiesarsagraph abuu  hiptsg =t the pFre  Software Fgndeaions'staillnegaaddessi fromthe saample GPL ot ice. Te mFSF as aohang_daaddessies{n ahe fart,
$nd 'my cdoaso again. Linux alrgldypnclude/ alccnpyovf$he fGPL.n";. jtere'vet 
}	}
	# ceck =or mKconfi:{hnlp hxt  hvenegaaGegll decriptson 	# Only{lplites we n addsg =th (nt	r cr igisal y,a ter 'hiatGw =oe ot peve 
#1pufixcint	tont|xt  o sdlersmne =wheth|r s" s ancdn_dalon;=enough.		$f (!kegllile {  s/Kconfi:/=&
			   okine =~ /^\s+s+*confi:s*+) {
		$fy $tivnth,= s0;		$	y $lcne= "$egllcne;		$	y $lin  0_inesn =+1;
		$	y $lf;			$y @$ii_tatrt= s0;		$	y $lii_ed a 00
		$	or m$
$fcne=>0;=&
 efined $rines [rin=-01}
 sin+; {
		$		$f  0_ines [rin=-01}
		$		$cne- in ($line  [rin=-01}=~ /^\-);
			p	tii_ed a 0line  [rin=-01}=~ /^\\+)
			m	$ext if (!lf=  /^\-);
			p	ast if ($dkile)&& -s t~ /^\s@\@) 
				$	n ($line  [rin=-01}=~ /^\s+s+*?:[bool|tritat e)s*\("/ {
		$fpp$ii_tatrt= s1
	}m		}elsif ($line  [rin=-01}=~ /^\s+s+*?:[---)?hnlp?:[---)?$/ {
		$fpp$ivnth,= s-1
	}m		}
		$		$f   s@^\.//;
}			$f   s@^#.*//;
}			$f   s@^\s*+//

	m	$ext if (!lf=  /^\$);
			p	f (!lf=  /^\s+*confi:s*/ {
		$fpp$ii_ed a 01;		cm		ast 
	m	$l}	m	$p$ivnth,+;;		$	
		c$f ($lni_tatrt=& -sii_ed a& kklvnth,=<$lmnc_amnf_decr_ingth,;=
		$		ARN(?"CONFIG_DESCRIPTION)
					    #"aiads owipte=asarsagraph hiatGdecripbes he Gontfi:{symbol ful yn";. jtere'cure;;
}		
			f#rint " ni_tatrt<lni_tatrt> ii_ed <sii_ed > ength,<klvnth,>n";
		f}
	# discouage\tee oaditionsovf$CONFIG_EXPERIMENTAL{n aKconfi:.		$f (!kegllile {  s/Kconfi:/=&
			   okine =~ /^.#s*efpxnd anes*+.*\bEXPERIMENTAL\b/ =
		$	ARN(?"CONFIG_EXPERIMENTAL)
				    #"Us ovf$CONFIG_EXPERIMENTAL{nsiefpe'ctode. Fgr altr naiovss,ksee=https://lkm_.org/lkm_/2012/10/23/580n";;;
}	 	}	ff ($!kegllile {  s/Makeile .*/{|| tegllile {  s/Kuild_.// {&
			   o(kine =~ /^\+(EXTRA_[A-Z]+FLAGS).//  {
		$fy $tflag= q$1
m		fy $lrpolac int	t s
		$		'EXTRA_AFLAGS' =>   'asflags-y',		$		'EXTRA_CFLAGS' =>   'ccflags-y',		$		'EXTRA_CPPFLAGS' => 'cppflags-y',		$		'EXTRA_LDFLAGS' =>  'ldflags-y',		$	}
				$ARN(?"DEPRECATED_VARIABLE)
				    #"Us ovf$tflag=nsiefpe'ctode, piads ose m\`lrpolac int	->{tflag}=nfstad .n";. jtere'cure;=f (!kegolac int	->{tflag};;
}	}
	# ceck =or mDT=ommpaiole =docuent	tion_
	ff (defined $root/=& 		$	$!kegllile {  s/\.dtsi?$/=& kklne =~ /^\\+s+*compaiole s+*=s*\("/ {||			d(!kegllile {  s/\.[ch]$/=& kklne =~ /^\\+.*\.compaiole s+*=s*\("/ ) {
				my @@compais ~$_rawines{  s/\"([a-zA-Z0-9\-\,\.\+_]+)("/g
				$y @$dt_atchn=$root/=.#"/Docuent	tion_/deviceere /blndng:s/;;}m	fy $lvp_ile { "$dt_atchn.#"vxndor-pefix ee.txt;
	}	f	oreach my $lcompai (@compais){
		$		y $icompai2= 0$hompai
		$		$compai2=  s/^s,[a-zA-Z0-9]*\-^s,<\.\*>\-^;		$		y $icompai3= 0$hompai
		$		$compai3=  s/^s,([a-z]*)[0-9]*\-^s,$1<\.\*>\-^;		$		`gego -Erq0"scompai|$compai2|$compai3""$dt_atch`
			p	f (! $? >> 8  =
		$			ARN(?"UNDOCUMENTED_DT_STRING, 		$			    #"DT=ommpaiole =tring =\"scompai\"tlpler}snun-docuent	d =-- ceck =$dt_atchn";. jtere'cure;;
}		l}	
	m	$ext if (lcompai ~ /^\([a-zA-Z0-9\-]+)(,^;		$		y $ivxndor  '$1;		cm	`gego -Eq0"^ivxndor\\b"$lvp_ile `
			p	f (! $? >> 8  =
		$			ARN(?"UNDOCUMENTED_DT_STRING, 		$			    #"DT=ommpaiole =tring =vxndor \"svxndor\"tlpler}snun-docuent	d =-- ceck =$vp_ile n";. jtere'cure;;
}			 		$f 	}m 	}# ceck =w oare n aa valid ppr c_tilesif (ot phe n tnore_ahes ahunk			ext if (!lrgllile {~ //\.(h|c|s|S|pl|sh|dtsi|dts)$);
	
#ine =lsnth,=liit"		$f ($line =~ /^\s+/=& kkoefvrawines{~ //\/s*\*/&& 			   okrawines ~ /^\.#s*s*\s*s@Nnent|s*/&& 			   o!(klne =~ /^\\+s+*$loguncAtin  s*$\(s+*?:[(KERN_\S+s+*|[^"*())?"[Xt]*d"s+*?:[\,|\)s+*;)s*$/){||			   oklne =~ /^\\+s+*"[^"*("s+*?:[s+*|,|\)s+*;)s*$/) {&
			   okrgllile {e ""cripts//ceck arch-.pl"=&
			   okisnth,=>$lmax_lne _ingth,;
 	
		$	ARN(?"LONG_INE )
				    #"lies=verltlmax_lne _ingth, har}actrnsn";. jtere'cure;;
}	}
	# Ceck =or mse r-visole =tring s brok n acrosstine)s,khich_{brglks he Galfizt

#'o sgego or mhe Gtring .  Make excepton  =we n he ioefvnous tring =xnd an aa
#'aewine (@multipl Gine) an aon =tring =cnstant)}cr m'\t',m'\r',m';',mr m'{'	# (ammin_ n ainines=fsiembly}cr ms alcoctll \123cr mhexadecimll \xaf alues		$f ($line =~ /^\s+s+*"/&& 			   okoefvine =~ /^"s*$/){& 			   okoefvrawines{~ //?:[s\?:[[t	r]|[0-7]{1,3}|x[0-9a-fA-F]{1,2})|;s+*|\{s*\)"s*$/) {
		$fARN(?"SPLIT_STRING, 		$	    #"qutesddtring =plit( acrosstine)sn";. jtere'oefv;;
}	}
	# ceck =or missiongoa ipacs{n aa tring =cnsctointion_
	ff (dkoefvrawines{~ /^[^\\]\w"$/=& kkeawines=~ /^\s+[\t ]+"\w) {
		$fARN(?'MISSING_SPACE' 		$	    #"brglk qutesddtring s atGa ipacs{har}actrnn";. jtere'oefv;;
}	}
	# ceck =or mupacs abefre_alcqutesddaewine 		$f (!keawines=~ /^^.*\".*\s\\n/ {
		$	f (!ARN(?"QUOTED_WHITESPACE_BEFORE_NEWINE )
					 "unne_essasy w iteupacs{befre_=lcqutesddaewine n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^^(\+.*\".*\s+(\\n/$1\\n/

		$
				}
	# ceck =or maddsg =ine) aith uu  adaewine .		$f ($line =~ /^\s+/=& kefined $rines [rinesn ]=& $rines [rinesn ]=~ /^\s\ None wines atGed af aille) {
		$fARN(?"MISSING_EOF_NEWINE )
				    #"addsg =anine =wth uu  e wines atGed af aillen";. jtere'cure;;
}	}
	# Blackine:ose mhi/lo macros		$f (!kegllile {  sm@rch /blackine/.*\.S$@ {
		m	f ($line =~ /^\.[lL][[:upacs:]]*=.*&[[:upacs:]]*0x[fF][fF][fF][fF]/){
		$		y $iere'vet{ 0"sere'\n;. jcat_vet!lines; .#"n";;}m	f	ERROR($LO_MACRO)
					    # "ue( ee oLO() macro, ot  (... & 0xFFFF)n";. jtere'vet ;		$	
		c$f ($line =~ /^\.[hH][[:upacs:]]*=.*>>[[:upacs:]]*16/){
		$		y $iere'vet{ 0"sere'\n;. jcat_vet!lines; .#"n";;}m	f	ERROR($HI_MACRO)
					    # "ue( ee oHI() macro, ot  (... >> 16)n";. jtere'vet ;		$	
		c 	}# ceck =w oare n aa valid ppr c_tilesiCcr mormlif (ot phe n tnore_ahes ahunk			ext if (!lrgllile {~ //\.(h|c|pl|dtsi|dts)$);
	
# atthe Gbegienng  f a kines=fdythab amst bcom ailrtl$nd 'fdyh n g	# mre_ahean 8 mst bue( eab .		$f (!keawines=~ /^\s+s+* \|s*\\S/{||			    keawines=~ /^\s+s+*        s*\/ {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lrawines; .#"n";;}m	f$rpt_heaner  = s1;
}	ff (dERROR($CODE_INDENT)
					  $coedbncdnt	ntoruld ue( eab  hire' ossible n";. jtere'vet =& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^\s+([ \|]+)^"s+;. jtalfiy(l1;/e;		$	
		c 	}# ceck =or mupacs{befre_=eab .		$f (!keawines=~ /^\s+/=& kkeawines=~ /^ \|/ {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lrawines; .#"n";;}m	ff (dARN(?"SPACE_BEFORE_TAB)
					"aiads , otmupacs{befre_=eab n";. jtere'vet =& 		$	   #rix) {
		$fphile ($silesd[lix;inesn ]=  		$			   /^(^\+.* {
8,8}\|/$1\t\|/ {
}		$fphile ($silesd[lix;inesn ]=  		$			   /^(^\+.* {+\|/$1\t/ {
}		$f
		c 	}# ceck =or m& ko '|| atthe Gaatrtof a kine 		$f (!keawines=~ /^^s+s+*?&&|\|\|/) {
		$fCHK("LOGICAL_CONTINUATIONS)
				  # "Logicll cntinuetion_i toruld b pon he ioefvnous ine n";. jtere'oefv;;
}	}
	# ceck =multi-ines=tat eint	tncdnt	tion_ mtch-) aoefvnous ine 		$f (!k^V=& kk^V=ge 5.10.0&& 			   okoefvine =~ /^\s+([ \|]*)(?:[$c90_Keywords?:[s++if)s*$)|?:[$Declares*$)??:[$nent|$\(s+*s*\s*Nnent|s*\\))s*$|Nnent|s*\=\s*Nnent|s*\)\(.*([&\&\[\[\|,)s*$/) {
		$fkoefvine =~ /^\s+(\t\)(*)/$/
m		fy $loldncdnt	' '$1
m		fy $lrpst= 1$2;	m		fy $lps = qosslast_ipen prent(frs t;;}m	ff ($lps => 0  =
		$		kine =~ /^\(s+| )([ \|]*)^;		$		y $ie wncdnt	' '$2;	m		f	y $igoodtalfcdnt	' '$oldncdnt	' 		fp		"\t"1x1$lps =/ 8)' 		fp		" " 1x1$lps =% 8);m		f	y $igoodupacsfcdnt	' '$oldncdnt	'  " " 1x1fps 
				p	f (!ie wncdnt	'n_=$goodtalfcdnt	'& 		$		   #re wncdnt	'n_=$goodupacsfcdnt	 {
				mp	f (!CHK("PARENTHESIS_ALIGNMENT)
							"Algnment	 toruld mtch- pen aprentee sisn";. jtere'oefv;=& 		$			$  #rix)=& kklne =~ /^\\+) {
		$f$fp$ixesd[lix;inesn ]=  		$f$fp    /^\s+[ \|]*/s+$goodtalfcdnt	^;		$		$
		cn	 	m	$ 	}m 	}	ff ($line =~ /^\s+.*\(s*$/ype\s*\\)[ \|]+(?!Nssignment	|$Arth metic|{)/ {
		$	f (!CHK("SPACING, 		$		"Ntmupacs{s ane_essasy  ter 'a ase n" . jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^(\(s*$/ype\s*\\))[ \|]+/$1/;	m	$ 	}m 	}	ff ($lrgllile {  s/@\(driersi/net/|net/)@{& 			   okoefvrawines{~ /^\s+[ \|]*\/s*[ \|]*$/&& 			   okrawines ~ /^\s+[ \|]*\*/&& 			   okrgllyne_{>02 {
		$	ARN(?"NETWORKING_BLOCK_COMMENT_STYLE)
				    #"networing  bocak&cnmint	s do't wusetan empty /* ine ,wuset/* Cnmint	...n";. jtere'oefv;;
}	}
		ff ($lrgllile {  s/@\(driersi/net/|net/)@{& 			   okoefvrawines{~ /^\s+[ \|]*\/s*/&& 		#aatrtng  /*			   okoefvrawines{~ //s*\/[ \|]*$/&& cn#eo hrilbnega*/			   okrawines ~ /^\s+/&& cn	#ine =s anew			   okrawines ~ /^\s+[ \|]*\*/ {
	n#eo iading: *		$	ARN(?"NETWORKING_BLOCK_COMMENT_STYLE)
				    #"networing  bocak&cnmint	s aatrtoith $*pon pubtequnt	 ine)sn";. jtere'oefv;;
}	}
		ff ($lrgllile {  s/@\(driersi/net/|net/)@{& 			   okrawines ~ /m@\s+[ \|]*\*/[ \|]*$@{& 	#hrilbnega*/			   okrawines ~ /m@\s+.*/s*.*\*/[ \|]*$@{& 	#inines=/*...*/			   okrawines ~ /m@\s+.*\*{2,}/[ \|]*$@{& 	#hrilbnega**/			   okrawines ~ /m@\s+[ \|]*.+s*\/[ \|]*$@ {
	#aon blank */				ARN(?"NETWORKING_BLOCK_COMMENT_STYLE)
				    #"networing  bocak&cnmint	s pt ahe freilbnega*/pon a searsate liesn";. jtere'cure;;
}	}
	# ceck =or missiongoblank iness=fter 'truct|/unon_ declartion_i	# ith $excepton  =or mvarious attributs =fd 'macros		$f (!koefvine =~ /^\[\+ ]};?s*$/){& 			   okines ~ /^\s+/&& 			   o!(klne =~ /^\\+s+*$/{||			    # llne =~ /^\\+s+*EXPORT_SYMBOL/{||			    # llne =~ /^\\+s+*MODULE_/i{||			   o# llne =~ /^\\+s+*\#s+*?:[end|elif|lsie)/{||			    # llne =~ /^\\+[a-z_]*neit/{||			    # llne =~ /^\\+s+*?:[tat ics++)?[A-Z_]*ATTR/{||			    # llne =~ /^\\+s+*DECLARE/{||			    # llne =~ /^\\+s+*__st up/  {
		$ff (!CHK("INE _SPACING, 		$		"Piads ose ma blank ines=fter 'fncAtin /truct|/unon_/enum declartion_in";. jtere'oefv;=& 		$	   #rix) {
		$fpiix_nfsete_ine_(lix;inesn , "s+; ;		$	
		c 	}# ceck =or miultipl Gcnst'cutiv pblank iness		$f (!koefvine =~ /^\[\+ ]s*$/){& 			   okines ~ /^\s+s*$/){& 			   okist_iblank_ine_{!= $hine)nr=-01  {
		$ff (!CHK("INE _SPACING, 		$		"Piads odo't wusetiultipl Gblank inessn";. jtere'oefv;=& 		$	   #rix) {
		$fpiix_deleed_ine_(lix;inesn , lrawines;;}m	$
				$$ist_iblank_ine_{ 0hine)nr;
}	}
	# ceck =or missiongoblank iness=fter 'declartion_i		ff ($luines ~ /^\s+s*(\S/{& cn	#Nt plt{har} 1			$# actull declartion_i		f   #!koefvine =~ /^\s+s+$MDeclares*$Nnent|s*\[=,;:\[]/{||				# fncAtin qosiner 'declartion_i		f    okoefvine =~ /^\s+s+$MDeclares*$\(s+*s*\s*Nnent|s*\\)s*\[=,;:\[\(]/{||				# foo bar; hire' foo isttom aocall ypesdefko '#efined		f    okoefvine =~ /^\s+s+$Mnent|?:[s++|s+*s*\s*)Nnent|s*\[=,;\[]/{||				# know_ declartion_'macros		$    okoefvine =~ /^\s+s+$Mdeclartion__macros) {&
				# for "lsie{if"khich_{one=looi likea"Nnent| Nnent|"			   o!(koefvine =~ /^\s+s+$Mc90_Keywords\b/{||				# oth|r ossible =extenion_sff tdeclartion_'iness		$     okoefvine =~ /^?:[$Cmmpare|Nssignment	|$Opratoors)s*$/){||				# ot psatrtng  a seAtin qre(a macro "s"=extened =lned		f    ookoefvine =~ /^?:[\{s*\|\\)$) {&
				# loois likealcdeclartion_			   o!(kuines ~ /^\s+s*(MDeclares*$Nnent|s*\[=,;:\[]/{||				# fncAtin qosiner 'declartion_i		f    o kuines ~ /^\s+s*(MDeclares*$\(s+*s*\s*Nnent|s*\\)s*\[=,;:\[\(]/{||				# foo bar; hire' foo isttom aocall ypesdefko '#efined		f    o kuines ~ /^\s+s*(Mnent|?:[s++|s+*s*\s*)Nnent|s*\[=,;\[]/{||				# know_ declartion_'macros		$    o kuines ~ /^\s+s*(Mdeclartion__macros){||				# aatrtof atruct|ko 'unon_ o 'enum		$    o kuines ~ /^\s+s*(?:[unon_|truct||enum|ypesdef)\b/{||				# aatrtofrGed af abocak&rt=cntinuetion_ f tdeclartion_		$    o kuines ~ /^\s+s*(?:[$|[\{s}\.\#s"\?\:\(s[])/{||				# bitfield=cntinuetion_		f    o kuines ~ /^\s+s*(Mnent|s*\[s+*\d+s*$[,;]/{||				# oth|r ossible =extenion_sff tdeclartion_'iness		$     okuines ~ /^\s+s*(\(?s+*?:[$Cmmpare|Nssignment	|$Opratoors)) {&
				# ncdnt	tion_ f toefvnous nd 'ourenth ines=fe vte nsamd		f    ((koefvine =~ /^s+(\++)\S) {& kksines ~ /^\s+$1\S)  {
		$	f (!ARN(?"INE _SPACING, 		$		 "Mssiongoa blank ines=fter 'declartion_in";. jtere'oefv;=& 		$	   #rix) {
		$fpiix_nfsete_ine_(lix;inesn , "s+; ;		$	
		c 	}# ceck =or mupacs aatthe Gbegienng  f a kines.	# Excepton  :	#  1;{ith n acnmint	s	#  2 {icdnt	deape'oec_essrt=cnmmnd s	#  3)ohndgsg =iabel 		$f ($lrawines ~ /^\s+ /=& kklne =~ /^\s+ *?:[$;|#|Nnent|:)) {{
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lrawines; .#"n";;}m	ff (dARN(?"LEADING_SPACE, 		$		 "aiads , otmupacss atthe Gaatrtof a kine n";. jtere'vet =& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^\s+([ \|]+)^"s+;. jtalfiy(l1;/e;		$	
		c 	}# ceck =w oare n aa valid C ppr c_tilesif (ot phe n tnore_ahes ahunk			ext if (!lrgllile {~ //\.(h|c)$);
	
# ceck =ncdnt	tion_ f tany1lne =wth oa bare lsie	# (bu  eot=f (itms alcmultipl Gine) "f (!foo) eturn ibar; lsie{eturn ibaz;")}# gf he ioefvnous ine ms alcbrglk rt=eturn ind bns{icdnt	dea1jtal mre_...		ff ($luines ~ /^\s+([t]*+)?:[}[ \|]*)?lsie?:[[ \|]*{)?s*$/) {
		$fy $itab  = ength,(l1)=+s1;
}	ff (dkoefvine =~ /^\s+st{itab ,itab }brglk\b/{||				   #!koefvine =~ /^\s+st{itab ,itab }eturn \b/{& 		$	   #kefined $rines [rinesn ]=& 		$	   #krines [rinesn ]=~ /^\[ \+]st{itab ,itab }eturn /) =
		$		ARN(?"UNNECESSARY_ELSE)
					    #"lsie{is eot=genratllywuseful  ter 'a brglk rt=eturn n";. jtere'oefv;;
}		
		c 	}# ceck =ncdnt	tion_ f ta1lne =wth oa brglk;}# gf he ioefvnous ine ms alcgoto rt=eturn ind bns{icdnt	deate nsamd # f ahab 	$ff ($luines ~ /^\s+([t]*+)brglk\s*;s*$/) {
		$fy $itab  = $1;
}	ff (dkoefvine =~ /^\s+itab ?:[goto|eturn )\b/ =
		$		ARN(?"UNNECESSARY_BREAK)
					    #"brglk is eot=useful  ter 'a goto rt=eturn n";. jtere'oefv;;
}		
		c 	}# discouage\tee oaditionsovf$CONFIG_EXPERIMENTAL{n a#ifdefi).		$f ($line =~ /^\s+s+*\#s+*if.*\bCONFIG_EXPERIMENTAL\b/ =
		$	ARN(?"CONFIG_EXPERIMENTAL)
				    #"Us ovf$CONFIG_EXPERIMENTAL{nsiefpe'ctode. Fgr altr naiovss,ksee=https://lkm_.org/lkm_/2012/10/23/580n";;;
}	 	}# ceck =or mRCS/CVSwreeison_'marker 		$f ($lrawines ~ /^\s+.*\$(Reeison_|Log|Id)?:[s$|/) {
		$fARN(?"CVS_KEYWORD)
				    #"CVSttayls keyword'marker ,therse wil t_eot_ b pupdtoden"; jtere'cure;;
}	}
	# Blackine:odo't wuset__uildinu_bine_[cs]sync		$f ($line =~ /^__uildinu_bine_csync/ {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lines; .#"n";;}m	fERROR($CSYNC)
				    # "ue( ee oCSYNC() macro n aasm/blackine.hn";. jtere'vet ;		$}		$f ($line =~ /^__uildinu_bine_ssync/ {
			$y @$ere'vet{ 0"sere'\n;. jcat_vet!lines; .#"n";;}m	fERROR($SSYNC)
				    # "ue( ee oSSYNC() macro n aasm/blackine.hn";. jtere'vet ;		$}	}# ceck =or moldtHOTPLUGt__dev<foo> seAtin qmarkng|s
}	f ($line =~ /^\b(__dev(neit|exit)(dtoa|cnsta|))\b/ =
		$	ARN(?"HOTPLUG_SECTION)
				    #"Ussg =$1{nsiunne_essasyn";. jtere'cure;;
}	}
	# Ceck =or mpotnt	ial 'bare' ypess
}	y @($tat ,@$amnd,kkine)_nr_ext , lreailn_ext , loff_ext ,			   okrgllyne__ext ;;
#rint " INE <kine)>n";

}	f ($line nr=>=@$tupressi_tat eint	t& 			   okrgllcne=& kkuines ~ /^.s*\\S/ =
		$	($tat ,@$amnd,kkine)_nr_ext , lreailn_ext , loff_ext )= 		cn	ctx_tat eint	_bocak$line nr,okrgllcne,0  

$fp$tat =  s/^sn.^sn g;
		$	lamnd=  s/^sn.^sn g;
	
#rint " ine nr<line nr> <$tat >n";
		ff# Ifthis =tat eint	tas aotmuat eint	tbgndeariesnith n 				# nt(here' s aotmosine n aeturyng  a sat eint	tsone				# ut	ilhweshitGed af ait.		fpy $lfrag= q$sat ;$lfrag=  s/^;+s*$/)/;
}	ff (dkfrag=~ //?:[{|;/) {
	#rint " skip<kine)_nr_ext >n";
		ff	$tupressi_tat eint	t skine)_nr_ext ;}m	$
				$# Find{he Gegll nxt =lne .	f}	lrgllyne__ext t skine)_nr_ext ;}m	$f (defined $rogllyne__ext t& 		$	   #$!efined $rines [rogllyne__ext t-01}=||				   # hubtrf$line  [rogllyne__ext t-01}, loff_ext )=  /^\s+*$/) =
		$		rogllyne__ext +;;		$	
			fpy $ls= q$sat ;
$fp$t=  s/^{.$/)/s;}	m	$# Inore_agoto iabel .	f	cf ($$t=  s/Nnent|:\$/)s {
				m# Inore_afncAtin  =benegacalyd 		$	
elsif ($kt=~ /^^.\s*Nnent|s*\\()s {
				m
elsif ($kt=~ /^^.\s*lsie\b/s {
				m# declartion_i always aatrtoith $ypess
}	m
elsif ($koefvvalues =eq"'E'=& kku=~ /^^.\s*?:[$Stoage\s++)??:[$nniness++)??:[cnstas++)???:[s+*Nnent|)+?)\b?:[s++$Sarse|)?s*$\**\s*?:[$nent|$\(s*[^\)]\\))?:[s+*NMdifiier)?s+*?:[;|=|,|\()/s){
		$		y $iypes= @$1;		cm	iypes=  s/^s+$/ g;
		$		ossible ($typ , "A:;. jtt;;}	m	$# efineiton_i n aglobll scoes=one=only{aatrtoith $ypess
}	m
elsif ($ku=~ /^^.?:[$Stoage\s++)??:[$nniness++)??:[cnstas++)??Nnent|)\bs+*?:!:)/s){
		$		ossible ($1, "B:;. jtt;;}m	$
				$# any1!foo ... *)ms alcosiner 'crt,
$nd 'foo ista$ypes			$hile ($su=~ /^\(?Nnent|)?:[s++$Sarse|)*[\s\*]+s+*\)/sg){
		$		ossible ($1, "C:;. jtt;;}m	$
				$# Ceck =or m ny1sortof afncAtin qdeclartion_.				# nc wiro(tom h n gibar, oth|r baz)
		ff# voi '(*stoae_gd|)?x86_decrr_ote{*;;}m	ff ($lpefvvalues =eq"'E'=& kku=~ /^^(.?:[ypesdefs*$)??:[?:[$Stoage\|$nnines)s*$)*s*$/ype\s*\?:[sb$nent|$\(s*\s*Nnent|s))s*$)\()s {
		$		y $($ame _ing) = ength,(l1);	m		f	y $ictx= q$s
		$		hubtrf$lctx, 0, $ame _ing=+s1, '';
			p	tctx=  s/^s)[^\)]\/)/;
		$fpir mi $iarg (plit((^s+*,s*\/,$ictx; =
		$			f ($larg ~ /^^(:[cnstas++)??Nnent|)?:[s++$Sarse|)*s*$\**\s*?:?sb$nent|)?$/s{|| targ ~ /^^($nent|)/)s {
				m$		ossible ($1, "D:;. jtt;;}m	$	$
		cn	 	m	$ 	
}	}
	#	# Ceck skhich_{yy cb panchord =n ahe Gont|xt .	#		# Ceck =or msithh_{() nd 'fssocitodeaase_fnd 'defaul 
#'tat eint	i toruld b patthe Gaamd ncdnt	.	f}f ($klne   ^\bsithh_s*\\(.*\)/ {
			$y @$ere{ 0'';			$y @$sep{ 0'';			$y @@ctx= qctx_bocak_ouer $line nr,okrgllcne;;}m	$shift(@ctx;;}m	$ir mi $ictx=(@ctx;{
		$		y $($cing,$icncdnt	)  'ine__tat  !lctx;;}m	$$f ($lctx=  s^\\+s+*?ase_s++|defaul :)/=& 		$			$	tncent| !=$icncdnt	) 
		$			$ere{. ""$seplctxn";
		ff		$sep{ 0'';			$l}elsie{
		$			$sep{ 0"[...]n";
		ff	}	m	$
			ff ($lere{n ='' {
			$	ERROR($SWITCH_CASE_INDENT_LEVEL)
					    # "sithh_{nd 'ose_ftoruld b patthe Gaamd ncdnt	\nsere'ines$ere; ;		$	
		c 	}# if/hile /etcGbrck =oe ot pgopon nxt =lne , utlss tefineig  a oe hile (loop,}# r msf hiatGbrck =on he inxt =lne  istor mppm h n gilsie	}	f ($line =~ /^(*)/\b??:[if|hile |or |sithh_|?:[[a-z_]+|)or _ach [a-z_]+)s*\((|do\b|lsie\b)/=& kklne =~ /^\.s*\\#) {
			$y @$oae_ctx= q"$1$2";	m		fy $(tivvel, @ctx;{ qctx_tat eint	_ivvel$line nr,okrgllcne,0  

			ff ($line =~ /^\s+st{6,}/ =
		$		ARN(?"DEEP_INDENTATION)
					    #"Too many1leding: tab  -Gcnstier {coedbrefactoang:n";. jtere'cure;;
}		
				$y @$ctx_cne= "$egllcne -G$#ctx=-1;
		$	y $lctx= qjsin("n";, @ctx;;				$y @$ctx_in  0_inesn 
		$	y $lctx_skip= "$egllcne;				$hile ($sctx_skip=>@$ctx_cne=|| $sctx_skip===@$ctx_cne=& 		$			efined $rines [rctx_in -01}=& 		$			rines [rctx_in -01}=  /^\-); {
		$fp##rint " SKIP<sctx_skip> CNT<$ctx_cne>n";
		ff	$ctx_skip- in ($!efined $rines [rctx_in -01}=|| tines [rctx_in -01}=~ /^\-);
			p	tctx_in+;;		$	
			fp#rint " egllcne<$egllcne> ctx_cne<$ctx_cne>n";
		ff#rint " oae<$oae_ctx>n"ines<kine)>n"ctx<$ctx>n"nxt <tines [rctx_in -01}>n";

	wn$f ($lctx=~ /^{s*\/=& kefined (tines [rctx_in -01})=& $rines [rctx_in -01}=  /^\\+s+*{) {
			$	ERROR($OPEN_BRACE)
					     #"hiatGpen abrck ={ toruld b pon he ioefvnous ine n";. 		$			"sere'\nrctxn"rrawines [rctx_in -01}n";)
	}	$
		cnf ($livvelt  0 =& kkoef_ctx=~ /^}s*$hile s*\(($/{& 		$	   #lctx=  s^\)s*\\;s*$/){& 				  #kefined $rines [rctx_in -01})				
		$		y $($aivnth,,$inncdnt	)  'ine__tat  !lines [rctx_in -01});}m	$$f ($lnncdnt	=>@$ncdnt	) 
		$			ARN(?"TRAILING_SEMICOLON)
						    #"reilbnegasemicoln_ n dtcte  aotmuat eint	s,bncdnt	nimlites oth|rwissn";. 		fp			"sere'\nrctxn"rrawines [rctx_in -01}n";)
	}	$	 		$f 	}m 	}# Ceck =e'iaiovsbncdnt	nfrt=cntitionsals nd 'bocaks.		$f ($line =~ /^\b?:[?:[if|hile |or |?:[[a-z_]+|)or _ach [a-z_]+)s*\((|do\b)/=& kklne =~ /^\.s*\#/=& kklne =~ /^\}s*$hile s*\/ =
		$	($tat ,@$amnd,kkine)_nr_ext , lreailn_ext , loff_ext )= 		cn	ctx_tat eint	_bocak$line nr,okrgllcne,0  		$			f ($!efined $ruat ;;}m	$y @($t,@$a;= @!ltat ,@$amnd;;				$hubtrf$ls, 0, ength,(lc), '';
			ff# Make srnsawehrpmov pte nine =pefix eetns weshve 		ff# non =on he iilrtl$lne , nd 'fe_agosg =t trgldd{he m		ff# hire' ne_essasy.
$fp$t=  s/^sn.^sn/gs;}	m	$# Find{uu  ow_ lon;=he Gontitionsal actullly{is.		fpy $@e winess= @!lc=~ /^\n/gs;;}m	$y @$onti_ines = s1=+s$#e winess;}	m	$# W iarn  o sceck =hiesilrtl$lne =nfsie/thiesbocak	m	$# satrtng  at=th (ntdovf$he fontitionsal,$so rpmov :	m	$#  1;{any1blank ines=ersmnetion_		f	#  2 {any1pen n gibrck ={ on ntdovf$he flned		f	#  3)oany1oe (... {
			$y @$cntinuetion_  s0;		$	y $lceck = 00;
$fp$t=  s/^^.*\bdo\b)/;
}	f$t=  s/^^s+*{)/;
}	ff (dkt=  s/^^s+*\\/) {
			$	$cntinuetion_  s1
	}	$
		cnf ($lt=  s/^^s+*?\n/) {
			$	$ceck = 01
	}	$	$onti_ines +;;		$	
			fp# Also tnore_aa(loop=cnstauct|kat=th (ntdovf$a		fp# pe'oec_essrt=tat eint	.	f	cf ($dkoefvine =~ /^\.s*\##s*effness+/{||				   #koefvine =~ /^s\s*$/) {&
@$cntinuetion_   0  =
		$		kceck = 00;
$fp
				$y @$cnti_ote{ 0-1
	}m	$cntinuetion_  s0;		$	hile ($scnti_ote{!=$icnti_ines  {
			$	$cnti_ote{ 0icnti_ines ;
		$fp# Iftwessee=an #lsie/#elifphe n he fond 		ffp# is eot=inesa .		m	$f (!ss=  /^\s+*\#s+*?:[esie|elif/) {
		$fp	kceck = 00;
$fpl}	
	m	$# Inore_:
	m	$#  1;{blank iness,theryftoruld b patt0,		$fp#  2 {pe'oec_essrt=iness,tnd 		$fp#  3)oiabel .	f	cff ($lcmtinuetion_ ||			d	   oku=  /^\s+*?\n/ ||			d	   oku=  /^\s+*#s+*?/ ||			d	   oku=  /^\s+*Mnent|s*\[) {
		$fpp$hmtinuetion_  s$ku=~ /^^.*?\\\n/ {?s1=:00;
$fplnf ($lt=  s/^^.*?\n/) {
			$	$	$onti_ines +;;		$		$
		cn	 	m	$ 	
}	$y @(ucdnf,$rugcdnt	)  'ine__tat  !"+;. jtt;;}m	$y @$sat _egll = raw_ine_(line nr,okcnti_ines  ;				$# Ceck =f (eith|r vf$he s Gine) afe_amdifiied,klsie	}		#(heisws aot phes aarch-'stoaul .	f	cf ($!efined (tsat _egll){||				   #ktat =! /^\s+/=& kksat _egll ! /^\s+/ =
		$		kceck = 00;
$fp
			$f (defined (tsat _egll){&
@$cnti_ines => 1;{
		$		ksat _egll = "[...]n"ksat _egll";		$	
			fp#rint " ines<kine)> oefvine <koefvine >bncdnt	<$ncdnt	> sncdnt	<$sncdnt	> ceck <kceck >=cntinuetion_<$hmtinuetion_> s<$s>=cnti_ines <$cnti_ines > sat _egll<ksat _egll> sat <$tat >n";
		wn$f ($lceck =&
@(($sncdnt	=% 8)=!=0 =||				   #!ksncdnt	=<=@$ncdnt	=& kku=n ='' ) =
		$		ARN(?"SUSPECT_CODE_INDENT)
					   # "suspectncoedbncdnt	nfrt=cntitionsal'tat eint	i ($ncdnt	,$rugcdnt	)n";. jtere'cure .#"ksat _eglln";)
	}	$
		c
			f# Trckk he f'alues ' acrosstont|xt  nd 'fded =lned .	f	y $lopine =~$rine ;$lopine =~ s@^\./ /;
}	y $($curevalues ,okcurevalrs)= 		cn	anot t evalues (lopine =.#"n";, lpefvvalues )
	}	$curevalues =~$rpefvvalues =. $curevalues 

}	f ($ldbgvalues ){
			$y @$uu ine =~$ropine ;@$uu ine =~ s/^st/ g;
		$	rint " line nr=> .$uu ine n";
		ffrint " line nr=> $curevalues n";
		ffrint " line nr=> okcurevalrsn";
		f}
$fkoefvvalues =~$hubtrf$lcurevalues ,o-1);	m#tnore_aines =ot pbenegafded 
		ext if (!line =~ /^\[\s+]);
	
# TEST:a l w_ directnts tig  f ahe frpes=mtch-)r.
}	f ($ldbgvrpes {
		m	f ($line =~ /^^.\s*NDeclares*$N) {
			$	ERROR($TEST_TYPE)
					     #"TEST:ais typsn";. jtere'cure;;
}		
elsif ($ldbgvrpes=> 1=& kklne =~ /^\.+(NDeclare)) {
			$	ERROR($TEST_NOT_TYPE)
					     #"TEST:ais ot phpes=($1{ns)n"; jtere'cure;;
}	p
			$ext ;}m	}
# TEST:a l w_ directnts tig  f ahe fattributs=mtch-)r.
}	f ($ldbgvattr {
		m	f ($line =~ /^^.\s*NMdifiiers*$N) {
			$	ERROR($TEST_ATTR)
					     #"TEST:ais attrn";. jtere'cure;;
}		
elsif ($ldbgvattr=> 1=& kklne =~ /^\.+(NMdifiier)) {
			$	ERROR($TEST_NOT_ATTR)
					     #"TEST:ais ot plttr=($1{ns)n"; jtere'cure;;
}	p
			$ext ;}m	}
}# ceck =or mneitoalistion_ to aggregte  apen abrck =on he inxt =lne 		$f ($line =~ /^^.\s*{/&& 			   okoefvine =~ /^?:$^|[^=])=s*$N) {
			$f (dERROR($OPEN_BRACE)
					  "hiatGpen abrck ={ toruld b pon he ioefvnous ine n";. jtere'oefv;=& 		$	   #rix)=& kkoefvine =~ /^\s+/=& kklne =~ /^\\+) {
			$	iix_deleed_ine_(lix;inesn  -01,okoefvrawines)
	}	$	iix_deleed_ine_(lix;inesn , lrawines;;}m	$	y $iilxedine =~$roefvrawines
	}	$	$ilxedine =~ s/^s+\=\s*N/t s
/
	}	$	iix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$	$ilxedine =~$rine ;		$fptilxedine =~ s/^^(.s*$)\{s*\/$1/;	m	$	iix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$ 	}m 	}#	# Ceck skhich_{fe_aanchord =on he ifded =lned.	#		# ceck =or mialirmaed=rtchs{n a#iclude/ttat eint	i (uies{RAW=lned)		$f ($lrawines ~ /m{\.s*\\#s+*iclude/\s+[<"](*)/[">]} {
			$y @$otchn=$r1;
}	ff (dkotchn= /m{//} {
			$	ERROR($MALFORMED_INCLUDE)
					     #"ialirmaed=#iclude/tole)ame n";. jtere'cure;;
}		
			ff ($kotchn= /"^uapi/"=&
 krgllile {  sm@\biclude//uapi/@ {
			$	ERROR($UAPI_INCLUDE)
					     #"No=#iclude/tn a...iclude//uapi/... toruld ue( a uapi/=rtch=pefix n";. jtere'cure;;
}		
			}
}# no C99 //acnmint	s		$f ($line =~ /m{//} {
			$f (dERROR($C99_COMMENTS)
					  $do ot pse mC99 //acnmint	sn" . jtere'cure;=& 		$	   #rix) {
		$fpy $line =~$rixesd[lix;inesn ];}m	$$f ($line =~ /^s^s^(*)/$/ {
			$	$y $icomint	t strim(l1);			$fptilxed[lix;inesn ]=  s/@s^s^(*)/$@/\*$icomint	t\*/@
	}	$	 		$f 	}m 		f# Rpmov pC99 cnmint	s.
}	line =~ /s@//.*@@
	}	lopine =~ s@@//.*@@
		# EXPORT_SYMBOL toruld iminditodlysirl w_ he fr n giitms aexportng ,Gcnstier 
#'oh =whole=tat eint	.	#rint " APW <tines [rogllyne__ext t-01}>n";

}	f ($efined $rogllyne__ext t& 		$   #exis	i rines [rogllyne__ext t-01}=& 			   o!efined $ruupressi_export{rogllyne__ext }{&
			   o(kine  [rogllyne__ext t-01}=~ /^EXPORT_SYMBOL.*\((*)/\)/{||			    #kine  [rogllyne__ext t-01}=~ /^EXPORT_UNUSED_SYMBOL.*\((*)/\)/) =
		$	# Hnd le efineiton_i hich_{producs{sdnt	iiiersnith 		$	# a=pefix :	m	$#   XXX!foo);	m	$#   EXPORT_SYMBOL(tom h n g_foo);	m	$y $ieamd =$r1;
}	ff (dktat =  s^^(:[.s*\}s*$\n)?.([A-Z_]+)s*\((s+*?$nent|)/{& 		$	   #leamd = s^^${nent|}_$2) {
	#rint " FOO C eamd<leamd>n";
		ff	$tupressi_export{rogllyne__ext }{ 01
	
}		
elsif ($ltat =! /^(:[		ff	\n.}s*$/|			d	^.DEFNE _Mnent|s(\Qleamd\E\)|			d	^.DECLARE_Mnent|s(\Qleamd\E\)|			d	^.LIST_HEADs(\Qleamd\E\)|			d	^.?:[$Stoage\s++)?/ype\s*\\(s+*s*\s*\Qleamd\E\*\\)s*\\(|			d	\b\Qleamd\E?:[s++$Attributs)*s+*?:[;|=|\[|\()		$	   #)/x {
	#rint " FOO A<tines [rogllyne__ext t-01}> sat <$tat > eamd<leamd>n";
		ff	$tupressi_export{rogllyne__ext }{ 02;
}		
elsie{
		$		ruupressi_export{rogllyne__ext }{ 01
		$f 	}m 		ff ($!efined $ruupressi_export{rinesn }&& 			   okoefvine =~ /^^.\s*N/{&
			   o(kine =~ /^EXPORT_SYMBOL.*\((*)/\)/{||			    #kine =~ /^EXPORT_UNUSED_SYMBOL.*\((*)/\)/) =
	#rint " FOO B <tines [rinesn  -01}>n";

}		ruupressi_export{rinesn }& 02;
}	}
}	f ($efined $ruupressi_export{rinesn }&& 			   okuupressi_export{rinesn }& =02 {
		$	ARN(?"EXPORT_SYMBOL)
				    #"EXPORT_SYMBOL(foo); toruld iminditodlysirl w_ i	i fncAtin /variablen";. jtere'cure;;
}	}
	# ceck =or mglobll neitoalisers.		$f ($line =~ /^\s+(s*$/ype\s*\Mnent|s*\?:[s++$Mdifiier))*s+*=\s*(0|NULL|fasie)\s*;) {
			$f (dERROR($GLOBAL_INITIALISERS)
					  $do ot pneitoalisemglobllsnho 0 r mNULLn";. 		fp	     #tere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^(/ype\s*\Mnent|s*\?:[s++$Mdifiier))*s+*=\s*(0|NULL|fasie)\s*;)r1;/;	m	$ 	}m 	# ceck =or muat ic neitoalisers.		$f ($line =~ /^\s+.*\btat ics+.*=\s*(0|NULL|fasie)\s*;) {
			$f (dERROR($INITIALISED_STATIC)
					  $do ot pneitoalisemtat icsnho 0 r mNULLn";. 		fp	     #tere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^(\btat ics+.*?)s+*=\s*(0|NULL|fasie)\s*;)r1;/;	m	$ 	}m 		# ceck =or missorer ed declartion_i of{har}/torrt/nt /lon;=ith $ugnoed/unugnoed
	$hile ($suine =~ /m{(\b/ype\Mssorer ed\b)}g){
		$	y $iympt strim(l1);			$ARN(?"MISORDERED_TYPE)
				    #"rpes='iymp' toruld b pspecfiiedtn a[[un]ugnoed] [torrt|nt |lon;|lon; lon;] orer n";. jtere'cure;;
}	}
	# ceck =or muat ic cnsta{har} *{ferays.		$f ($line =~ /^\btat ics++cnstas++har}s+*s*\s*(\w+)s*\([s*\(]s+*=\s*) {
		$fARN(?"STATIC_CONST_CHAR_ARRAY)
				    #"uat ic cnsta{har} *{feray toruld probabl cb puat ic cnsta{har} *{cnstas";. 		fp	tere'cure;;
               }
	# ceck =or muat ic car} foo[] = "bar" declartion_i.		$f ($line =~ /^\btat ics++car}s++(\w+)s*\([s*\(]s+*=\s*") {
		$fARN(?"STATIC_CONST_CHAR_ARRAY)
				    #"uat ic car} feray declartion_'toruld probabl cb puat ic cnsta{har}s";. 		fp	tere'cure;;
               }
	# ceck =or mnon-globll har} *foo[] = {"bar",a...} declartion_i.		$f ($line =~ /^^.\s+?:[tat ics++|cnstas++)?car}s++s*\s*\w+s*\([s*\(]s+*=\s*\{) {
		$fARN(?"STATIC_CONST_CHAR_ARRAY)
				    #"har} *{feray declartion_'migh pbepbeter 'aspuat ic cnstas";. 		fp	tere'cure;;
               }
	# ceck =or mfncAtin qdeclartion_ aith uu  arguint	i likea"nc wiro()"		$f ($line =~ /^(\b/ype\s*(Mnent|)s*\((s+*\)) {
			$f (dERROR($FUNCTION_WITHOUT_ARGS)
					  $BadmfncAtin qdeineiton_ -G$1()'toruld probabl cb p$1(voi )n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^(\b(/ype\\s+(?$nent|))s*\((s+*\))$2 $3(voi )/;	m	$ 	}m 		# ceck =or muies{of{DEFNE _PCI_DEVICE_TABLE		$f ($line =~ /^\bDEFNE _PCI_DEVICE_TABLEs*\((s+*?\w+)s*\()s+*=/ {
		$	f (!ARN(?"DEFNE _PCI_DEVICE_TABLE, 		$		 "Pefir 'truct| pci_device_id=verltefpe'ctode{DEFNE _PCI_DEVICE_TABLEn";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^\b?:[tat ics++|)DEFNE _PCI_DEVICE_TABLEs*\((s+*?\w+)s*\()s+*=\s*)uat ic cnsta{truct| pci_device_id=$1([s] = /;	m	$ 	}m 		# ceck =or mnew ypesdefs,=only{fncAtin qoartmlerss nd 'sarse| anot t on_i	# make sens .		$f ($line =~ /^\bypesdefs*){& 			   okines ! /^\bypesdefs*+/ype\s*\\(s+*s*?Nnent|s*\\)s*\\(){& 			   okines ! /^\bypesdefs*+/ype\s*+Nnent|s*\\(){& 			   okines ! /^\b$typ Tpesdefs\b/{& 		$   okines ! /^\b__bitwiss?:[__|)\b/ =
		$	ARN(?"NEW_TYPEDEFS)
				    #"do ot pldd{new ypesdefsn";. jtere'cure;;
}	}
	# * goes{on variable ot pon hpes			# (aar}*[ cnsta})			hile ($slne =~ /m(?\($NonptrTpes(s+*?:[NMdifiiersbs+*|s*\s*)+)s))}g){
		$	#rint " AA<t1>n";

}		y $($idnt	,$rfrom,$rto;= @!l1,ok2,ok2 ;				$# Soruld aatrtoith $a ipacs.
$fp$to=~ s/^^(\S)/{$1/;	m	$# Soruld ot pntdoith $a ipacs.
$fp$to=~ s/^s+$M/g

		$# '*'i toruld ot peve mupacs abetwee_.				hile ($sto=~ s/^s*s++s*/s*\*/){
		$	}
	##	ffrint " 1: from<rfrom>nho<sto>{sdnt	<$idnt	>n";

}		f (dkfrom'n_=$to;={}m	$$f ($ERROR($POINTER_LOCATION)
						  $\"(fookfrom)\" toruld b p\"(fookto;\"n";. jjtere'cure;=& 		$		   #rix) {
		$fppy @$sub_from'=@$ndnt	;		$fppy @$sub_to'=@$ndnt	;		$fpp$sub_to'= s/^sQkfrom\E/kto^;		$		$silesd[lix;inesn ]=  		$			   s/@sQ$sub_from\E@$sub_to@
	}	$	 		$f 	}m 		fhile ($slne =~ /m(?\b$NonptrTpes(s+*?:[NMdifiiersbs+*|s*\s*)+)?$nent|))}g){
		$	#rint " BB<t1>n";

}		y $($mtch-,$rfrom,$rto,@$ndnt	;= @!l1,ok2,ok2,ok3 ;				$# Soruld aatrtoith $a ipacs.
$fp$to=~ s/^^(\S)/{$1/;	m	$# Soruld ot pntdoith $a ipacs.
$fp$to=~ s/^s+$M/g

		$# '*'i toruld ot peve mupacs abetwee_.				hile ($sto=~ s/^s*s++s*/s*\*/){
		$	}
		$# Mdifiieri toruld eve mupacs .
$fp$to=~ s/^?\b$Mdifiier$)/$1 /;		##	ffrint " 2: from<rfrom>nho<sto>{sdnt	<$idnt	>n";

}		f (dkfrom'n_=$to=& -sient| ! s^^$Mdifiier$/;={}m	$$f ($ERROR($POINTER_LOCATION)
						  $\"fook{from}bar\" toruld b p\"fook{to}bar\"n";. jjtere'cure;=& 		$		   #rix) {
			$fppy @$sub_from'=@$mtch-;		$fppy @$sub_to'=@$mtch-;		$fpp$sub_to'= s/^sQkfrom\E/kto^;		$		$silesd[lix;inesn ]=  		$			   s/@sQ$sub_from\E@$sub_to@
	}	$	 		$f 	}m 		# # no BUG(}cr mBUG_ON()}# 	$f ($line =~ /^\b?BUG|BUG_ON)\b/ =
	# 	$frint " Trynho se mARN(_ON & Recverly{coedbrath|r hean BUG(}cr mBUG_ON()n";

# 	$frint " tere'cure;

# 	$f$heane= 00;
# 	$ 	}	ff ($line =~ /^\bLINUX_VERSION_CODE\b/ =
		$	ARN(?"LINUX_VERSION_CODE)
				    #"LINUX_VERSION_CODEftoruld b pavoi ed,kcoedbtoruld b por mhe Gersion_ to hich_{itms amergden";. jtere'cure;;
}	}
	# ceck =or muies{of{rint k_sateliit"		$f ($line =~ /^\brint k_sateliit"s*\\() =
		$	ARN(?"PRINTK_RATELIMITED)
	"Pefir 'rint k_sateliit"d =o 'ri_<ivvel>_sateliit"d =to rint k_sateliit"s";. jtere'cure;;
}	}
	# rint k toruld ue( KERN_* ivvels jjNote hiatGirl w_ n qoint k's=on he 
#'tamd ine =do ot pnn_daa ivvel, sotwesue( ee oourenth bocak&cnt|xt 
#'o s	r cnd 'fid 'fdd validat( ee oourenth oint k jjIn pummasy ee oourenth	# rint k nclude/ alll=pefceing: oint k's=hich_{eve mnone wines on he intd.	# ws=fsiuin=hiesilrtl$bad rint k ns he Gon =t trgport.		$f ($line =~ /^\brint k\((?!KERN_)\s*") {
		$fy @$u = 00;
$fpor m$y $lin  0_inesn =-1;
$lin >~$rixrtl_ine ;@$ln-- {
		$fp#rint " CHECK<tines [rin -01}n";
	}	$	# weshve  a=pefceing: oint k=f (itmxnd 	}	$	# wth $"n";.tnore_ait,klsie{itms at tblamd		f$	n ($line  [rin=-01}=~ /m{\brint k\(} {
			$	$f ($lrawines [rin=-01}=~ /m{\n";} {
			$	$	$u = 01;		cm		
		cn		ast 
	m	$l}	m	$
			ff ($ku =  0  =
		$		ARN(?"PRINTK_WITHOUT_KERN_LEVEL)
					    #"rint k()'toruld iclude/tKERN_ facfizt
 ivveln";. jtere'cure;;
}		
			}
}	$f ($line =~ /^\brint k\*\\(s+*KERN_([A-Z]+)/ {
			$y @$r ig' '$1
m		fy $livvelt  lc($r ig 

$fp$ivvelt  "war";.t ($livvelteq "war"ng:")
m		fy $livvel2= 0$ivvel

$fp$ivvel2= 0"dbg;.t ($livvelteq "debu:")
m		fARN(?"PREFER_PR_LEVEL)
				    #"Pefir '[hubtystem eg:ne tdev]_$ivvel2([hubtystem]dev,a...phe n dev_$ivvel2(dev,a...phe n ri_$ivvel(... =to rint k(KERN_$r ig'...n";. jtere'cure;;
}	}
		$f ($line =~ /^\bri_war"ng:s*\\() =
		$	f (!ARN(?"PREFER_PR_LEVEL)
					 "Pefir 'ri_war"(... to ri_war"ng:(...n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^\bri_war"ng:sb/ri_war"/;	m	$ 	}m 	}	ff ($line =~ /^\bdev_rint k\*\\(s+*KERN_([A-Z]+)/ {
			$y @$r ig' '$1
m		fy $livvelt  lc($r ig 

$fp$ivvelt  "war";.t ($livvelteq "war"ng:")
m		f$ivvelt  "dbg;.t ($livvelteq "debu:")
m		fARN(?"PREFER_DEV_LEVEL)
				    #"Pefir 'dev_$ivvel(... to dev_rint k(KERN_$r ig,'...n";. jtere'cure;;
}	}
	# fncAtin qbrck =ca't wb pon tamd ine ,$except=or m#efinedsff tde hile ,}# r msf closd =on tamd ine 
	cf ($dkine =~//ype\s*\Mnent|s(.*\).*\s*{) {nd 		$   o!$klne   ^\##s*effnes.*do\s{) {nd o!$klne   ^})  {
		$	f (!ERROR($OPEN_BRACE)
					  "pen abrck ='{'Girl w_ng: fncAtin qdeclartion_ agopon he inxt =lne n";. jtere'cure;=& 		$	   #rix) {
		$fpiix_deleed_ine_(lix;inesn , lrawines;;}m	$	y $iilxed_ine_{ 0hrawines
	}	$	$ilxed_ine =~ /^(^..$/ype\s*\Mnent|s(.*\)\s*){(*)/$/
m		fpy $line 1= @$1;		cm	y $line 2' '$2;		$fpiix_nfsete_ine_(lix;inesn , ltrim(line 1));		$fpiix_nfsete_ine_(lix;inesn , "\+{;)
	}	$	f ($line 2 ! /^\s*$N) {
			$	piix_nfsete_ine_(lix;inesn , "\+\t"1. trim(line 2));		$fp 		$f 	}m 		# pen abrck stor menum,'unon_ nd 'sruct| gopon he itamd ine .		$f ($line =~ /^^.\s*{/&& 			   okoefvine =~ /^^.\s*?:[ypesdefs*+)?(enum|unon_|truct|)?:[s++$nent|)?s*$N) {
			$f (dERROR($OPEN_BRACE)
					  "pen abrck ='{'Girl w_ng: $1 gopon he itamd ine n";. jtere'oefv;=& 		$	   #rix)=& kkoefvine =~ /^\s+/=& kklne =~ /^\\+) {
			$	iix_deleed_ine_(lix;inesn  -01,okoefvrawines)
	}	$	iix_deleed_ine_(lix;inesn , lrawines;;}m	$	y $iilxedine =~$rtrim(loefvrawines)'  " {"
	}	$	iix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$	$ilxedine =~$rrawines
	}	$	$ilxedine =~ s/^^(.s*$)\{s*\/$1\	^;		$		f (dkflxedine =! /^\s+s*$/) {
			$	piix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$	 		$f 	}m 		# issiongoupacs{ ter 'unon_,atruct|ko 'enum deineiton_		$f ($line =~ /^^.\s*?:[ypesdefs*+)?(enum|unon_|truct|)?:[s++$nent|){1,2}[=\{]) =
		$	f (!ARN(?"SPACING, 		$		 "issiongoupacs{ ter '$1 deineiton_n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^^(.s*$?:[ypesdefs*+)?(:[enum|unon_|truct|)?:[s++$nent|){1,2})([=\{])/$1 $2/;	m	$ 	}m 		# FncAtin qosiner 'declartion_i	# ceck =upacn gibetwee_ typ , fncAptr, nd 'fegi	# canonicll declartion_'s a"rpes=(*fncAptr)(fegi... "		$f ($line =~ /^^.\s*?NDeclare)\((s*$)\*(s*$)?Nnent|)?s*$)\)?s*$)\(/ {
			$y @$declare' '$1
m		fy $loef_osiner _upacs{ '$2;		$fy $lps t_osiner _upacs{ '$3;		$fy $lfncAeamd =$r4;		$fy $lps t_fncAeamd_upacs{ '$5
m		fy $loef_fegi_upacs{ '$6
		# he iNDeclare variable wil tcaptrnsalll=upacss ater 'he frpes
#'t sceck =it=or ma issiongoreilbnegaissiongoupacs{bu  osiner 'eturn iypess
#odo't wnn_daa upacs{t sdo't wwar"por mheos .		$fy $lps t_declare_upacs{ '";

}		f (dkdeclare'  /^(\*+)/) {
			$	lps t_declare_upacs{ '$1;		cm	ideclare' 'rtrim(ldeclare;;
}		
			ff ($kdeclare'~ //s*$/=& kkps t_declare_upacs{  s^^$/ =
		$		ARN(?"SPACING, 		$		    #"issiongoupacs{ ter 'eturn iypesn";. jtere'cure;;
}			lps t_declare_upacs{ '" ";		$	
		#iunne_essasyoupacs{"rpes==(*fncAptr)(fegi... "	# Tes ats tais ot pourenthly{impleint	ed b caue( ee s odeclartion_i ars
#'equivalen  o 
#	nt "wiro(nt "bar, ... 
# and{heisws airma'toruld't /does't wgenratt  a=ceck arch- war"ng:.	#	#	fflsif ($ldeclare'  /^\s{2,}$/ =
	#	$		ARN(?"SPACING, 	#	$		    #"Multipl Gupacss ater 'eturn iypesn";. jtere'cure;;
#	$	
		#iunne_essasyoupacs{"rpes=( *fncAptr)(fegi... "		$	f ($efined $roef_osiner _upacs{& 		$	   #roef_osiner _upacs{  /^\s*/ =
		$		ARN(?"SPACING, 		$		    #"Unne_essasyoupacs{fter 'fncAtin  osiner 'pen aprentee sisn";. jtere'cure;;
}		
		#iunne_essasyoupacs{"rpes=(* fncAptr)(fegi... "		$	f ($efined $ros t_osiner _upacs{& 		$	   #ros t_osiner _upacs{  /^\s*/ =
		$		ARN(?"SPACING, 		$		    #"Unne_essasyoupacs{befre_=fncAtin  osiner 'ame n";. jtere'cure;;
}		
		#iunne_essasyoupacs{"rpes=(*fncAptr )(fegi... "		$	f ($efined $ros t_fncAeamd_upacs{& 		$	   #ros t_fncAeamd_upacs{  /^\s*/ =
		$		ARN(?"SPACING, 		$		    #"Unne_essasyoupacs{fter 'fncAtin  osiner 'ame n";. jtere'cure;;
}		
		#iunne_essasyoupacs{"rpes=(*fncAptr) (fegi... "		$	f ($efined $roef_fegi_upacs{& 		$	   #roef_fegi_upacs{  /^\s*/ =
		$		ARN(?"SPACING, 		$		    #"Unne_essasyoupacs{befre_=fncAtin  osiner 'arguint	in";. jtere'cure;;
}		
				$f ($torwvrpes?"SPACING,){&
@$ix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^^(.s*$)MDeclares*$\(s+*s*\s*Nnent|s*\\)s*\\(/$1 .@$declare'.kkps t_declare_upacs{. '(*''.kkfncAeamd . ')('/ex;	m	$ 	}m 		# ceck =or mupacn girgnde square'brckkets;a l w_ed:	#  1.oith $a rpes=on he ileft - int "[] a;	#  2. atthe Gbegienng  f a kines=or mulics{seitoalisers - i[0...10] = 5,	#  3.=nfsie/tapourl cbrck =- i= {i[0...10] = 5  		fhile ($slne =~ /^(*)?s*)\[/g){
		$	y $($hire',okoefix) { @!l-[1}, l1;;}m	ff ($lpefix)=~ ///ype\s*+N/{& 		$	   #($hire'=!=0 =|| lpefix)=~ //^.\s+$) {&
				   #roefix)=~ //[{,]\s+$) {{}m	$$f ($ERROR($BRACKET_SPACE, 		$			  "upacs{prohibi	ed b fre_=pen asquare'brckket '['n";. jtere'cure;=& 		$		   #rix) {
		$fp   #rix)sd[lix;inesn ]=  		$			/^^(\+.*?)s++\[/$1([/
	}	$	 		$f 	}m 		# ceck =or mupacs abetwee_ fncAtin  =and{heeiraprentee ss .
$fhile ($slne =~ /^(Mnent|)s*+\(/g){
		$	y $ieamd =$r1;
}	fy $lctx_b fre_== hubtrf$line , 0, $-[1});
}	fy $lctx= q"$ctx_b fre_ieamd";}	m	$# Inore_aheos  directovss hire' upacs a_are_mormmitode.}m	ff ($leamd = s^^(:[		ff	if|or |hile |sithh_|eturn |ose_|			d	voiaioe |__voiaioe __|			d	__attributs__|irmaat|__extenion___|			d	asm|__asm__)/)x)				
		$	# cppm#efinedttat eint	i eve mnon-opionsal'tpacs , ie	}		#(ifphe r ms alcupacs{betwee_ te inamd and{hee=pen 	}		#(prentee sis{itms asimply ot plqoartmlers grgnp.}m	f
elsif ($lctx_b fre_== /^\.s*\\##s*effness+$/) {
			$	# cppm#elifptat eint	tcntitions{yy caatrtoith $a (}m	f
elsif ($lctx== /^\.s*\\##s*elifs+$/) {
			$	# Ifthis =whole=h n gsmxnd oith $a rpes=i	i ms t		$	# likely a ypesdefkor ma fncAtin .}m	f
elsif ($lctx=  s/Nype\/) {
			$	
elsie{
		$		f (!ARN(?"SPACING, 		$			 "upacs{prohibi	ed b twee_ fncAtin inamd and{pen aprentee sis '('n";. jtere'cure;=& 		$			    #kix) {
		$fppsilesd[lix;inesn ]=  		$			   s/^\b$ame n*+\(/$ame n(/
	}	$	 		$f 	}m 		# Ceck =opratoormupacn g.		$f ($!$klne   ^\##s*iclude//)){
		$	y $iilxed_ine_{ 0";

}		y $kine)_ilxed= 00;

}		y $kop =~$qr
		$fp<<=|>>=|<=|>=|==|!=|			d	\+=|-=|\*=|\/=|%=|\^=|\|=|&=|			d	=>|->|<<|>>|<|>|=|!|~|			d	&&\[\[\|,|\^|\+\+|--|&\[\|\+|-|\*|\/|%|			d	\?:|\?|[		ff}x

}		y $@eleint	i = hlit((^(lops|;/),$ropine );		##	ffrint ("eleint	tcnunt: <;. jt#eleint	i . ">n";)
	##	fffre_ah_{y @$el (@eleint	i =
	##	$		rint ("el: <$el>n";)
	##	ff 	
}	$y @@iix_eleint	i = ();
}	fy $loff= 00;

}		fre_ah_{y @$el (@eleint	i =
		$		rush(@iix_eleint	i, hubtrf$lrawines, loff, ength,(lel)));		$fploff=+= ength,(lel);
}		
				$loff= 00;

}		y @$blank =tcnpy_upacn g(ropine );	}		y $kist_ifter ' 0-1
	
}		frem$y $le= 00;$le=<jt#eleint	i;$le=+=02 {
	}m	$	y $igood=~$rixe_eleint	i[$n]'.kkfxe_eleint	i[$n=+s1];		##	ff	rint ("n: <$n> good: <$good>n";)
			$fploff=+= ength,(leleint	i[$n]);
		$fp# Pik =up he ioefceing: nd 'succeeing: aar}acters.		$	fy $lca== hubtrf$lopine , 0, $off;;}m	$	y $icc{ 0'';			$lf ($ength,(lopine ) >~$(loff=+ ength,(leleint	i[$n=+s1] ) =
		$			icc{ 0hubtrf$lopine , loff=+ ength,(leleint	i[$n=+s1] )
	}	$	 		$f	y $icb= q"$ca$;icc";	m		f	y $ia{ 0'';			$lia{ 0'V'.t ($leleint	i[$n]'n ='' ;			$lia{ 0'W'.t ($leleint	i[$n]'  /^\s$/ ;			$lia{ 0'C'.t ($leleint	i[$n]'  /^$;i/ ;			$lia{ 0'B'.t ($leleint	i[$n]'  /^(\[|\()i/ ;			$lia{ 0'O'.t ($leleint	i[$n]'eq"'' ;			$lia{ 0'E'.t ($lca== /^\s*$N) ;	m		f	y $iop= "$eleint	i[$n=+s1];			$f	y $ic{ 0'';			$lf ($efined $releint	i[$n=+s2] =
		$			ic{ 0'V'.t ($leleint	i[$n=+s2]'n ='' ;			$l	ic{ 0'W'.t ($leleint	i[$n=+s2]'  /^\s*/ ;			$l	ic{ 0'C'.t ($leleint	i[$n=+s2]'  /^\$;/ ;			$l	ic{ 0'B'.t ($leleint	i[$n=+s2]'  /^\(\)|\]|;/) ;			$l	ic{ 0'O'.t ($leleint	i[$n=+s2]'eq"'' ;			$l	ic{ 0'E'.t ($leleint	i[$n=+s2]'  /^\s**\\i/ ;			$l}elsie{
		$			$c{ 0'E'
	}	$	 			$f	y $ictx= q"${a}x${c}";	m		f	y $iat{ 0"(ctx:lctx;";	m		f	y $iote{ 0hubtrf$lblank, 0, $off; . "^";}m	$	y $iere'ote{ 0"sere'ines$ptrn";;
		$fp# Pull{uu  he Gelues f aheis=opratoor.m		f	y $iop_ypes= @hubtrf$lcurevalues ,oloff=+ 1, 1);
		$fp# Ge  he Gfull{upratoormvariat	.	f	c	y $iopv=~$rop . hubtrf$lcurevalrs, loff, 1);
		$fp# Inore_aupratoor aarssd =asqoartmlerss.	f	cff ($lop_ypes=n ='V'=& 		$		   #rca== /^\s$/{&
@$cc'  /^\s**,) {
		#	$fp# Inore_acnmint	s	#		$l}elsif ($lop'  /^\$;+/) {
			$	p# ; toruld eve meith|r th (ntdovf$ines oralcupacs{ora\{fter 't"		$$l}elsif ($lop'eq"';' {
			$	$f ($lctx=~ /^.x[WEBC]/=& 		$			    $cc'! /^\s\/{&
@$cc'! /^\;) {
			$	p$f ($ERROR($SPACING, 		$					  "upacs{requi ed ater 'heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rixe_eleint	i[$n]'.ktrim(lfxe_eleint	i[$n=+s1])'  " ";}m	$		}	line _ilxed= 01;		cm			
		cn		}	
	m	$# //ms alccnmint			$$l}elsif ($lop'eq"'//' {
			$	p#   :   whn apretof a kbitfield		$$l}elsif ($lopv'eq"':B' {
			$	$# skip=he Gbitfield=ts taor mnow			$	p# Ntmupacss or :
	m	$#   ->		$$l}elsif ($lop'eq"'->' {
			$	$f ($lctx=  /^Wx.|.xW) {
			$	p$f ($ERROR($SPACING, 		$					  "upacss{prohibi	ed argnde heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rtrim(lfxe_eleint	i[$n])'  trim(lfxe_eleint	i[$n=+s1]);}m	$		}	f ($efined $rfxe_eleint	i[$n=+s2] =
		$				ppsile_eleint	i[$n=+s2]=  s/^^s++//;}m	$		}	}}m	$		}	line _ilxed= 01;		cm			
		cn		}	
	m	$# , mustshve  a=upacs{o_ te irigh .		$$l}elsif ($lop'eq"',' {
			$	$f ($lctx=~ /^.x[WEC]/=& @$cc'! /^\}) {
			$	p$f ($ERROR($SPACING, 		$					  "upacs{requi ed ater 'heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rixe_eleint	i[$n]'.ktrim(lfxe_eleint	i[$n=+s1])'  " ";}m	$		}	line _ilxed= 01;		cm				kist_ifter ' 0$n;		cm			
		cn		}	
	m	$# '*'=asqoartof a kypes=deineiton_ --trgported alrgldy.		$$l}elsif ($lopv'eq"'*_' {
			$	$#war"p"'*'=isqoartof aypesn";;
		$fp# unasyoupratoor atoruld eve maoupacs{befre_=nd 		$fp# non =fter .  My cb pleft adjacsn  o sanoth|r		$fp# unasyoupratoor, oralccrt,		$$l}elsif ($lop'eq"'!'=|| lop'eq"'~' ||			d		 lopv'eq"'*U'=|| lopv'eq"'-U' ||			d		 lopv'eq"'&U'=|| lopv'eq"'&&U' {
			$	$f ($lctx=~ /^[WEBC]x./=& @$ca=! /^(:[\)|!|~|\*|-|\&\[\|\+\+|\-\-|\{/$/ {
			$	$$f ($ERROR($SPACING, 		$					  "upacs{requi ed befre_=heat{'lop'$iatn";. jtere'otr) =
		$					f ($le{!=$iist_ifter '+02 {
		$						$good=~$rixe_eleint	i[$n]'.k" "'.kltrim(lfxe_eleint	i[$n=+s1]);}m	$		}		line _ilxed= 01;		cm				}		cm			
		cn		}						f ($lop'eq"'*'{&
@$cc'  /\s*NMdifiiersb/ {
			$	$$# A unasyo'*'{yy cb pcnsta			$fpp
elsif ($lctx=  s/.xW) {
			$	p$f ($ERROR($SPACING, 		$					  "upacs{prohibi	ed ater 'heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rixe_eleint	i[$n]'.krtrim(lfxe_eleint	i[$n=+s1]);}m	$		}	f ($efined $rfxe_eleint	i[$n=+s2] =
		$				ppsile_eleint	i[$n=+s2]=  s/^^s++//;}m	$		}	}}m	$		}	line _ilxed= 01;		cm			
		cn		}	
	m	$# unasyo++ nd 'unasyo--tansalllw_ed otmupacs{o_ on =sie/.		$$l}elsif ($lop'eq"'++' oralop'eq"'--' {
			$	$f ($lctx=~ /^[WEOBC]x[^W]/=& @$ctx=~ /^[^W]x[WOBEC]/ {
			$	$$f ($ERROR($SPACING, 		$					  "upacs{requi ed on =sie/ f aheat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rixe_eleint	i[$n]'.ktrim(lfxe_eleint	i[$n=+s1])'  " ";}m	$		}	line _ilxed= 01;		cm			
		cn		}						f ($lctx=  /^Wx[BE]/{||				$	   #($ctx=  /^Wx./{&
@$cc'  /^\;/) {
			$	p$f ($ERROR($SPACING, 		$					  "upacs{prohibi	ed befre_=heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rtrim(lfxe_eleint	i[$n])'  trim(lfxe_eleint	i[$n=+s1]);}m	$		}	line _ilxed= 01;		cm			
		cn		}						f ($lctx=  /^ExW) {
			$	p$f ($ERROR($SPACING, 		$					  "upacs{prohibi	ed ater 'heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rixe_eleint	i[$n]'.ktrim(lfxe_eleint	i[$n=+s1]);}m	$		}	f ($efined $rfxe_eleint	i[$n=+s2] =
		$				ppsile_eleint	i[$n=+s2]=  s/^^s++//;}m	$		}	}}m	$		}	line _ilxed= 01;		cm			
		cn		}	
	m	$# << nd '>>{yy ceith|r eve mr mno peve mupacs aboh $ugdss
}	ml}elsif ($lop'eq"'<<' oralop'eq"'>>' or			d		 lop'eq"'&' oralop'eq"'^' oralop'eq"'|' or			d		 lop'eq"'+' oralop'eq"'-' or			d		 lop'eq"'*' oralop'eq"'/' or			d		 lop'eq"'%' 		$		
			$	$f ($lctx=  /^Wx[^WCE]|[^WCE]xW) {
			$	p$f ($ERROR($SPACING, 		$					  "nn_dacnstistnt	mupacn giargnde 'lop'$iatn";. jtere'otr) =
		$					$good=~$rtrim(lfxe_eleint	i[$n])'  " "'.ktrim(lfxe_eleint	i[$n=+s1])'  " ";}m	$		}	f ($efined $rfxe_eleint	i[$n=+s2] =
		$				ppsile_eleint	i[$n=+s2]=  s/^^s++//;}m	$		}	}}m	$		}	line _ilxed= 01;		cm			
		cn		}	
	m	$# A coln_ nn_ds otmupacss befre_=whn aitms 
	m	$# ersmnetiog  a ose_felues fralciabel.		$$l}elsif ($lopv'eq"':C'=|| lopv'eq"':L' {
			$	$f ($lctx=  /^Wx.) {
			$	p$f ($ERROR($SPACING, 		$					  "upacs{prohibi	ed befre_=heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rtrim(lfxe_eleint	i[$n])'  trim(lfxe_eleint	i[$n=+s1]);}m	$		}	line _ilxed= 01;		cm			
		cn		}	
	m	$# All{hee=pth|rswnn_daupacs aboh $ugdss.		$$l}elsif ($lctx=~ /^[EWC]x[CWE]/ {
			$	$y @$u = 00;
			$	$# Inore_aeaillplddessis a<foo@bar>			$	$f ($$lop'eq"'<'=& 		$			    #kcc'  /^\sS+\@sS+>/){||				$	   #($op'eq"'>'=& 		$			    #kca== /^<sS+\@sS+$/) 		$			{		$			    	$u = 01;		cm		
				$	$# messag) afe_aERROR,{bu  ?:afe_aCHK						f ($lo =  0  =
		$			$y @$msg_ypes= @\&ERROR;		cm			$msg_ypes= @\&CHK f ($$lop'eq"'?:'=|| lop'eq"'?'=|| lop'eq"':')=& @$ctx== /^VxV) ;	m		f			f ($&{$msg_ypes}($SPACING, 		$						 "upacss{requi ed argnde heat{'lop'$iatn";. jtere'otr) =
		$					$good=~$rtrim(lfxe_eleint	i[$n])'  " "'.ktrim(lfxe_eleint	i[$n=+s1])'  " ";}m	$		}	f ($efined $rfxe_eleint	i[$n=+s2] =
		$				ppsile_eleint	i[$n=+s2]=  s/^^s++//;}m	$		}	}}m	$		}	line _ilxed= 01;		cm			
		cn		}	cn		}	cn		loff=+= ength,(leleint	i[$n=+s1]);}	##	ff	rint ("n: <$n> GOOD: <$good>n";)
			$fplilxed_ine_{ 0hilxed_ine_{.$igood;
}		
				$f ($(t#eleint	i %02 {  0  =
		$		kilxed_ine_{ 0hilxed_ine_{.$iile_eleint	i[$#eleint	i];
}		
				$f ($rix)=& kkine _ilxed=&
@$ix)ed_ine_{n_=$ilesd[lix;inesn ] {
		$fp$ixesd[lix;inesn ]= @$ix)ed_ine_;
}		
		
}m 		# ceck =or mwhi	eupacs{befre_=nmnon-nak_dauemicoln_
	$f ($line =~ /^\s+.*\Ss++;s*$N) {
			$f (dARN(?"SPACING, 		$		 "upacs{prohibi	ed befre_=uemicoln_n";. jtere'cure;=& 		$	   #rix) {
		$fp1 hile ($ixesd[lix;inesn ]=  
f$fp    /^^(s+.*\S)s*+;)r1;/;	m	$ 	}m 		# ceck =or miultipl Gasugnoint	s		$f ($line =~ /^^.\s*NLelus+\=\s*NLelus+\=(?!=)/ {
			$CHK("MULTIPLE_ASSIGNMENTS)
				   #"iultipl Gasugnoint	sftoruld b pavoi edn";. jtere'cure;;
}	}
	## # ceck =or miultipl Gdeclartion_i,alllw_ng: fr ma fncAtin Gdeclartion_	## # cmtinuetion_.	## 	$f ($line =~ /^^.\s*Nype\s*+Nnent|?:[s+*=[^,{]*)?s*$,\s*Nnent|.\/=& 	## 	$   okines ! /^^.\s*Nype\s*+Nnent|?:[s+*=[^,{]*)?s*$,\s*Nype\s*\Mnent|.*/){
	##	## 	$f# Rpmov pany1brckket_daueAtin  =to ensrnsawehdo ot 	## 	$f# fasilytrgport he ioartmlerss f afncAtin s.	## 	$	y $lin  0_ines;	## 	$	hile ($sln'= s/^s([^\(\)]\\)//g){
	## 	$	}	## 	$	f ($lin'= s/,/){
	## 	$	$ARN(?"MULTIPLE_DECLARATION)
	##	$		    #"declarnegaiultipl Gvariable =togeth|r toruld b pavoi edn";. jtere'cure;;
## 	$	}	## 	$}
	#nn_daupacs befre_=brck =irl w_ng: if, hile , etc
	cf ($dkine == /^\(.*\){/=& kklne =~ /^\(/ype\s){/){||			   okines = /^do{) {
			$f (dERROR($SPACING, 		$		  "upacs{requi ed befre_=hee pen abrck ='{'n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^^(s+.*?:[do|\)))\{/$1 {/;	m	$ 	}m 		## # ceck =or mblank iness befre_=declartion_i	##	$f ($line =~ /^^.\t+Nype\s*+Nnent|?:[s+*=.*)?;/=& 	##		   okoefvrawines ~ /^^.\s*N/ =
	##	$	ARN(?"SPACING, 	##$		    #"Nomblank iness befre_=declartion_in";. jtere'oefv;
	##	f}	##		# closn gibrck =toruld eve maoupacs{irl w_ng: it=whn aitmha =anyh n g	# pn$he flned		ff ($line =~ /^}(?!?:[,|;|\)))\S) {
			$f (dERROR($SPACING, 		$		  "upacs{requi ed ater 'heat{closdabrck ='}'n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^}((?!?:[,|;|\)))\S)/}{$1/;	m	$ 	}m 		# ceck =upacn gio asquare'brckkets		$f ($line =~ /^([s*/=& kklne =~ /^\[s*$N) {
			$f (dERROR($SPACING, 		$		  "upacs{prohibi	ed ater 'heat{pen asquare'brckket '['n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^\[s++/([/
	}	$ 	}m 		ff ($line =~ /^(s\]) {
			$f (dERROR($SPACING, 		$		  "upacs{prohibi	ed befre_=heat{closdasquare'brckket ']'n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^\s+\])\]);	m	$ 	}m 		# ceck =upacn gio aprentee ss 		ff ($line =~ /^((s*/=& kklne =~ /^\(s+*?:[\\)?$/{& 		$   okines ! /^fres*$\(s++;/=& kklne =~ /^\s+s*$[A-Z_][A-Z\d_]$\(s+*sd+(\,.*)?\)\,?N) {
			$f (dERROR($SPACING, 		$		  "upacs{prohibi	ed ater 'heat{pen aprentee sis '('n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^\(s++/((/
	}	$ 	}m 		ff ($line =~ /^(\*+)\)/=& kklne =~ /^\.s*\\)/{& 		$   okines ! /^fres*$\(.*;\s+\)/{& 		$   okines ! /^:\s+\)/ {
			$f (dERROR($SPACING, 		$		  "upacs{prohibi	ed befre_=heat{closdaprentee sis ')'n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^\s+\)/\)/;	m	$ 	}m 		# ceck =unne_essasyoprentee ss  argnde lddessiof/er efr enk =tn ge ($Lelus
#oie: &(foo->bar) toruld b p&foo->bar nd '*(foo->bar) toruld b p*foo->bar	
}mhile ($slne =~ /^(?:[^&]&s+*|s*)((s+*?$nent|s+*?:[NMembers*$)+)s*\()/g){
		$	CHK("UNNECESSARY_PARENTHESES)
				   #"Unne_essasyoprentee ss  argnde $1n";. jtere'cure;;
}	   # 		#goto iabel afe_'t wgcdnt	ed,k l w_ a=tn ge (upacs{howev|r		$f ($line =~/^.\s+[A-Za-z\d_]+:(?![0-9]+)/{nd 		$   !$line =~/^. [A-Za-z\d_]+:) {nd o!$klne   ^^.\s+efiaul :)  {
		$	f (!ARN(?"INDENTED_LABEL)
					 "iabel atoruld ot pbewgcdnt	edn";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^^(.)s*+/$1/;	m	$ 	}m 		# eturn iis ot plafncAtin 
	$f (defined (tsat )=& @$tat =  s^^.s*\eturn ?s*$)\(/ ){
			$y @$upacn gi=$r1;
}	ff (dk^V=& @$^V=ge 5.10.0=& 		$	   #rtat =  s^^.s*\eturn s+*?$balanced_prents)\s*;s*$/) {
			$	y @$elues  '$1;		cm	ielues  'deprentee size(ielues)
	}	$	f ($lelues  ~ m^\s*$NFncAArgs+*?:[\?|$/) {
		$fp	ERROR($RETURN_PARENTHESES)
				p	     #"eturn iis ot plafncAtin ,oprentee ss  are ot prequi edn";. jtere'cure;;
}			 		$f elsif ($ltpacn gi~ /^\s+) {
			$	ERROR($SPACING, 		$		     #"upacs{requi ed befre_=hee pen aprentee sis '('n";. jtere'cure;;	m	$ 	}m 		# unne_essasyoeturn iinplavoi afncAtin 
# attend-of-fncAtin ,oith $he ioefvnous ine  a=tn ge (leaing: tab,phe n eturn ;
# and{hed ine =befre_=heat{ot plagoto iabel target likea"out:"		$f ($lsines ~ /^^[ \+]}s*$//&& 			   okoefvine =~ /^^\+\teturn s+*;s*$//&& 			   okinesn  >= 3&& 			   okines [rinesn  -03] ~ /^^[ +]/&& 			   okines [rinesn  -03] ! /^^[ +]\s*Nnent|s*\[) {
		$fARN(?"RETURN_VOID)
				    #"voi afncAtin oeturn itat eint	i are ot pgenratlly{usefuln";. jtere'oefv;
	                		# ifptat eint	s{usng: unne_essasyoprentee ss  -oie: f ($dfoo{  0bar))		$f ($l^V=& @$^V=ge 5.10.0=& 		$   okines = /^\bifs+$(?:[\?s*$){2,})/ {
			$y @$ren prents =$r1;
}	fy $lcnunt=~$ropn prents =~ tr@\(@\(@;
}	fy $lmsg{ '";

}		f (dkines = /^\bifs+$(:[\?s*$){lcnunt,lcnunt}$LeluOrFncAs+*?$Compare)\s*NLeluOrFncA?:[s+*\)){lcnunt,lcnunt}) {
			$	y @$compt s$4;	#Nt p$1 b caue( of NLeluOrFncA		cm	imsg{ '" -omaybew  0toruld b p= ?;.t ($lcompteq "==;)
	}	$	ARN(?"UNNECESSARY_PARENTHESES)
					    #"Unne_essasyoprentee ss imsgn";. jtere'cure;;	m	$ 	}m 		# Rturn iof weat{appear at tbe=an errotmuoruld otrmtlly{be=-'v 		$f ($line =~ /^^.\s*eturn s+*?E[A-Z]*)\s*;) {
			$y $ieamd =$r1;
}	ff (dkeamd n ='EOF'{&
@$eamd n ='ERROR' {
			$	ARN(?"USE_NEGATIVE_ERRNO)
					    #"rturn iof an errotmuoruld typicllly{be=-e m(rturn i-$1)n";. jtere'cure;;	m	$ 	}m 		# Nn_daa upacs{b fre_=pen aprentee sis fter 'tf, hile  etc
	cf ($line =~ /^\b?if|hile |or |sithh_)\() =
		$	f (!ERROR($SPACING, 		$		  "upacs{requi ed befre_=hee pen aprentee sis '('n";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  
f$fp    /^\b?if|hile |or |sithh_)\()$1 ((/
	}	$ 	}m 		# Ceck =or mnllegalGasugnoint	iinpsf cntitionsal'--tand{ceck =or mreilbneg
#itat eint	i ater 'he fcntitionsal.		$f ($line =~ /^do\s$(:!{)/ {
			$(tsat ,okcnti,kkine _nr_ext , lreailn_ext , loff_ext )= 		cn	ctx_tat eint	_bocak(line nr,okogllct	,$0 		$			f ($!efined $ruat );
}	fy $(tsat _ext )=  ctx_tat eint	_bocak(line _nr_ext ,		$				lreailn_ext , loff_ext );
}	ftsat _ext '= s/^sn.^sn g;
		$	##rint " sat <$tat > sat _ext <tsat _ext >n";
		wn$f ($lsat _ext '= s^\s*$hile sb/ {
			$	# Ifthieptat eint	tcarris  leaing: e winess,		$fp# he n cnunt=heos  as f fsets.		$	fy $($hii	eupacs)= 		cn		$lsat _ext '= s^\(?:[s+*\n[+-])*s+*)/s;;}m	$	y $if fset= 		cn		tat eint	_rawines ($hii	eupacs)=-01
	
}			ruupressi_hile reilbers{line _nr_ext  +		$						loffset}{ 01
		$f 	}m 		ff ($!efined $ruupressi_hile reilbers{line n }&& 			   oefined (tsat )=& @efined (tcnti)=& 		$   okines = /^\b?:[if|hile |or )s*\\(){& kklne =~ /^\.s*\#) {
			$y $(ts,okc { @!lsat ,okcnti)
		wn$f ($lc = /^\bifs+$\(.*[^<>!=]=[^=].*/ ){
			$	ERROR($ASSIGN_IN_IF, 		$		     #"do ot pse masugnoint	iinpsf cntitionsn";. jtere'cure;;
}		
				$# Fid 'uu  weat{is on he intd f ahe fine  ater 'he 			$# cntitionsal.		$	hubtrf$ls, 0, ength,(lc),"'' ;			$$s'= s/^sn.*/g;
		$	$s'= s/^$;/g;
 f# Rpmov pany1cnmint	s		$lf ($ength,(lc)=& @$t ! /^\s*${?s**\\*s*$//&& 				    $c ! /^}s*$hile ss*) 				
			$	# Fid 'uu  how lon;'he fcntitionsal actullly{is.		$	fy $@e winess{ @!lc = /^\n/gs;;}m	$	y $icnti_ines = 01 + $#e winess;}m	$	y $isat _egll = '';	
}			ruat _egll = raw_ine_(line nr,okcnti_ines )		$					.$"n";.t ($lcoti_ines )
	}	$	f ($efined (tsat _egll)=& @$cnti_ines => 1 =
		$			iuat _egll = "[...]\niuat _egll";
}			 				$	ERROR($TRAILING_STATEMENTS)
					     #"reilbnegatat eint	i toruld b pon nxt =lne n";. jtere'cure. jtsat _egll)
	}	$ 	}m 		# Ceck =or mbitwiss=ts ts writodn as booeane		$f ($line =~ /^			$(:[		ff	?:[s[|\(|\&\&\[\[\)		$		ss*0[xX][0-9]+ss*		ff	?:[s&\&\[\[\)		$	|		ff	?:[s&\&\[\[\)		$		ss*0[xX][0-9]+ss*		ff	?:[s&\&\[\[\|\)|\])		$	))x)			
		$fARN(?"HEXADECIMAL_BOOLEAN_TEST)
				    #"booeane=ts taith $hexadecimtl,mormhaps justs1 (&{ora\|?n";. jtere'cure;;
}	}
	# iftand{lsie{toruld ot peve mgenratlitat eint	i ater 't"		$f ($line =~ /^^.\s*?:[}s*$)?lsie\b?.*)/ {
			$y @$s =$r1;
}	f$s'= s/^$;/g;
 f# Rpmov pany1cnmint	s		$lf ($$t ! /^\s*$?:[s+if|?:[{|)s*\\\?s*$N)) {
			$	ERROR($TRAILING_STATEMENTS)
					     #"reilbnegatat eint	i toruld b pon nxt =lne n";. jtere'cure)
	}	$ 	}m 	# ifptoruld ot pcmtinue  a=brck 		$f ($line =~ /^}s*$ifsb/ {
			$ERROR($TRAILING_STATEMENTS)
				     #"reilbnegatat eint	i toruld b pon nxt =lne  (oradi ayou mane='lsie{if'?)s";. 		fp	tere'cure;;
}m 	# ose_fand{efiaul {toruld ot peve mgenratlitat eint	i ater 'he m		$f ($line =~ /^^.\s*?:[ose_\s*.*|efiaul s*$):/g{& 		$   okines ! /^\G(:[		ff?:[s+*$;*)?:[s+*{)?(:[s+*$;*)?:[s+*\\)?\*$/|			d\s*eturn s++		$   o))xg)			
		$fERROR($TRAILING_STATEMENTS)
				     #"reilbnegatat eint	i toruld b pon nxt =lne n";. jtere'cure;;
}	}
		$# Ceck =or m}<nl>lsie{
, ee s omustsbe=at he itamd		$# gcdnt	 ivvelat tbe=reivvan  o s_ah_{pth|r.		$f ($loefvine   ^}s*$//&and{klne   ^^.\s*lsie\s*/&& 			   okoefvgcdnt	 ==@$nndnt	;=
		$	f (!ERROR($ELSE_AFTER_BRACE)
					  "lsie{toruld irl w_ closdabrck ='}'n";. jtere'oefv;=& 		$	   #rix)=& kkoefvine =~ /^\s+/=& kklne =~ /^\\+) {
			$	iix_deleed_ine_(lix;inesn  -01,okoefvrawines)
	}	$	iix_deleed_ine_(lix;inesn , lrawines;;}m	$	y $iilxedine =~$roefvrawines
	}	$	$ilxedine =~ s/^}s*$//^;		$		f (dkflxedine =! /^\s+s*$/) {
			$	piix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$	 		$f	$ilxedine =~$rrawines
	}	$	$ilxedine =~ s/^^(.s*$)lsie)$1}elsie/;	m	$	iix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$ 	}m 	}	$f ($loefvine   ^}s*$//&and{klne   ^^.\s*hile ss*)&& 			   okoefvgcdnt	 ==@$nndnt	;=
		$	y $(ts,okc { @ctx_tat eint	_bocak(line nr,okogllct	,$0 ;				$# Fid 'uu  weat{is on he intd f ahe fine  ater 'he 			$# cntitionsal.		$	hubtrf$ls, 0, ength,(lc),"'' ;			$$s'= s/^sn.*/g;
			$lf ($$t = s^\s*$;) {
			$	f (!ERROR($WHILE_AFTER_BRACE)
						  "hile  toruld irl w_ closdabrck ='}'n";. jtere'oefv;=& 		$		   #rix)=& kkoefvine =~ /^\s+/=& kklne =~ /^\\+) {
			$		iix_deleed_ine_(lix;inesn  -01,okoefvrawines)
	}	$		iix_deleed_ine_(lix;inesn , lrawines;;}m	$		y $iilxedine =~$roefvrawines
	}	$		y $iyeilbnega~$rrawines
	}	$		iyeilbnega~ s/^^s+//;}m	$		iyeilbnega~$trim(lyeilbneg);			$fptilxedine =~ s/^}s*$//}$iyeilbneg/;			$	piix_nfsete_ine_(lix;inesn , $ilxedine )
	}	$	 		$f 	}m 		#Specfiic variable ts ts
	fhile ($slne =~ /m(?$Cnstaan |NLelu)}g){
		$	y $ivar =$r1;

#gccmbinasyoextenion_		$lf ($$var = /^\$Binasy$) {{}m	$$f ($ARN(?"GCC_BINARY_CONSTANT, 		$			 "Avoi agccmv4.3+mbinasyocnstaan oextenion_: <$var>n";. jtere'cure;=& 		$		   #rix) {
		$fp	y $ierxvll = srint f("0x%x", oct$$var));			$fptilxed[lix;inesn ]=  		$			   s/^\b$varsb/ierxvll/
	}	$	 		$f 	
#CamdlCse_		$lf ($$var ! s^^$Cnstaan //&& 				    $var = /^[A-Z][a-z]|[a-z][A-Z]/=& 	#Inore_aPage<foo>mvariat	s				    $var ! s^^(:[Ceanr|Set|Ts tCeanr|Ts tSet|)Page[A-Z]/=& 	#Inore_aSIatayl Gvariat	i likeanS, mVfand{eB (ie: max_uV,=regultoor_min_uA_torw)				    $var ! s^^(:[[a-z_]*?)_?[a-z][A-Z]?:[_[a-z_]+)?/) {{}m	$$hile ($svar = /m(?$nent|)}g){
		$	p	y $iword  '$1;		cm		nxt =f ($$word ! /^[A-Z][a-z]|[a-z][A-Z]/);			$fpt ($lceck  =
		$				seed_camdlose__nclude/ ();}m	$		}f ($!lixls{&  !$camdlose__ixls_seeded =
		$				pseed_camdlose__ixls(kogllixls);}m	$		}	lcamdlose__ixls_seeded= 01;		cm			
		cn		}						f ($!efined $rcamdlose_{$word} {
			$	$	$camdlose_{$word}= 01;		cm			CHK("CAMELCASE)
							   #"Avoi aCamdlCse_: <$word>n";. jtere'cure;;		cn		}					 		$f 	}m 		#otmupacss lllw_ed ater '\iinpefined		$f ($line =~ /^\##s*effnes.*\\\s+$) {{}m	$f ($ARN(?"WHITESPACE_AFTER_LNE _CONTINUATION)
					 "Whi	eupacs{ater '\\ makes nxt =lne s{uselesin";. jtere'cure;=& 		$	   #rix) {
		$fp$ixesd[lix;inesn ]=  s/^s+$M/g

		$ 	}m 		#war"pf (<asm/foo.h>{is #nclude/ 'fdd <ineux/foo.h>{is avilbable (uies{RAW=lne )		$f ($ltres{&  $rawines ~ /m{\.s*\\##s*nclude/s*\\<asm\/(*)/\.h\>}){
		$	y $iixls{ 0"s1.h;

}		y $kceck ixls{ 0"iclude//ineux/iixls;

}		f (d-f0"sroot/kceck ixls"=& 		$	   #rogllixls{n_=$ceck ixls{& 		$	   #r1=~ ///lllw_ed_asm_nclude/ ) 				
			$	f ($logllixls{~ /m{\arch/} {
			$	$CHK("ARCH_INCLUDE_LINUX)
				p	    "Cnstidr 'usng: #nclude/ <ineux/iixls>=nfstead f a<asm/iixls>n";. jtere'cure;;
}			 elsie{
		$			ARN(?"INCLUDE_LINUX)
				p	     "Uie{#nclude/ <ineux/iixls>=nfstead f a<asm/iixls>n";. jtere'cure;;
}			 
		$ 	}m 		#aiulti-tat eint	tmacroi toruld b penclosd =inplade hile  loop, grab he 
#'ilrtl$tat eint	tand{lnsrnsai	i he fwhole=macro=f (its ot pntclosd 	# inplaknown goodpcmtianesr		$f ($logllixls{~ /m@/vmineux.lds.h$@=& 		$   okines = /^\.s*\\##s*effness+$/nent|?\()?/ {
			$y @$in  0_inesn 

}		y $kcnt=~$rogllct	=-01
		$	y $(toff, $dsat ,okdcnti,kkrs t);
}	fy $lctx= q'';			$y $ieas_flw__tat eint	  00;
$fpy $ieas_arg_cntca	  00;
$fp($dsat ,okdcnti,kkln,$kcnt, $off;  		cn	ctx_tat eint	_bocak(line nr,okogllct	,$0 ;			$$ctx= q$dsat 
		$	#rint " dsat <$dtat > dcnti<kdcnti> ct	<$ct	> f f<$off>n";

}		#rint " LNE <tines [rin-1]> eng<;. jength,(lines [rin-1])'  "n";
		wn$ieas_flw__tat eint	  01 f ($lctx=  /^\b(goto|eturn )\b/ ;	wn$ieas_arg_cntca	  01 f ($lctx=  /^\#\#) ;	m		f$dtat a~ s/^^.s*\\##s*effness++Nnent|?:[s([^\)]\\))?\*$/g

		$$dtat a~ s/^$;/g;

		$$dtat a~ s/^\\\n./g;

		$$dtat a~ s/^^\*$/gs

		$$dtat a~ s/^\*$//^s;				$# Fltoodn anyoprentee ss  and{brck s
	$$hile ($sdtat a~ s/^\([^\(\)]\\)/1/{||				       sdtat a~ s/^\{[^\{\}]\\}/1/{||				       sdtat a~ s/^\[[^\[\]]\\]/1/ 				
			$
				$# Extremely lon;'macroi yy cflll=off=he intd f ahe 			$# avilbable cnt|xt aith uu  closn g.  Gie maodangbneg
		$# backslashthe Gbenffnt f ahe fdoub	tand{ l w_ it		$	# to gobble anyohangn giopen-prents.
		$$dtat a~ s/^\(.+\\i/1/;				$# Fltoodn anyoobvnous trfng: antca	nteatin .}m	fhile ($sdtat a~ s/^("X*")s+$/nent|/$1/{||				       sdtat a~ s/^$nent|s+*?"X*")/$1/ 				
			$
				$y @$exceptin  =~$qr
		$fpMDeclare|		ff	moduls_oartm_eamdd|		ff	MODULE_PARM_DESC|		ff	DECLARE_PER_CPU|		ff	DEFNE _PER_CPU|		ff	CLK_[A-Z\d_]+|			d	__ypesof__\(|			d	unon_|			d	truct||			d	\.$nent|s+*=\s*|			d	^\"|\"$			$
x

}		#rint " REST<krs t> dsat <$dtat > ctx<lctx>n";

}		f (dkdtat an =''{& 		$	   #rdtat a! s^^(:[$nent||-?$Cnstaan ),//&& 		$# 10, //miro(),		$	   #rdtat a! s^^(:[$nent||-?$Cnstaan );//&& 		$# iro();		$	   #rdtat a! s^^[!~-]?(:[NLelu|$Cnstaan )//&& 		# 10 //miro() //m!foo{//m~foo{//m-foo{//mfoo->bar //mfoo.bar->baz		$	   #rdtat a! s^^'X'$/{&
@$dtat a! s^^'XX'$/{&
		$# aar}acterocnstaan s		$	   #rdtat a! s^$exceptin  /{& 		$	   #rdtat a! s^^\.$nent|s+*=/{&
		$$# .foo{ 		$	   #rdtat a! s^^(:[\##s*$nent||\##s*$Cnstaan )s*$//&& 	$# srfng:fiiction_'#foo		$	   #rdtat a! s^^do\s$$Cnstaan \s*hile ss*$Cnstaan ;?$/{& 	#odo {...} hile  (...); //mdo {...} hile  (...)		$	   #rdtat a! s^^fres*$$Cnstaan //&& 			$# irr (...)		$	   #rdtat a! s^^fres*$$Cnstaan s++(:[$nent||-?$Cnstaan )$/{& 	#oirr (...)0bar()		$	   #rdtat a! s^^do\s${/&& 			$	#odo {...		$	   #rdtat a! s^^\({/&& 			$		# ({...		$	   #rctx=~ /^\.s*\##s*effness++TRACE_(:[SYSTEM|INCLUDE_FILE|INCLUDE_PATH)\b/ 				
			$	lctx=  //^sn$//^;		$		y $iere'ctx= q$e r m  "n";
		$		y $icnt=~$tat eint	_rawines ($ctx;;	
}			frem$y $le= 00;$le=<jtcnt;$le++ =
		$			iere'ctx=.= raw_ine_(line nr,okn)m  "n";
		$		 				$	f (dkdtat a  /^;) {
		$fp	ERROR($MULTISTATEMENT_MACRO_USE_DO_WHILE)
				p	     #"Macroi ith $iultipl Gtat eint	i toruld b penclosd =inplade - hile  loopn";. j"iere'ctx";;
}			 elsie{
		$			ERROR($COMPLEX_MACRO)
				p	     #"Macroi ith $complexfeluesi toruld b penclosd =inpprentee ss n";. j"iere'ctx";;
}			 		$f 	
# ceck =or miacroi ith $f w_ cnt|rol,{bu  ith uu  ## antca	nttion_	# ## antca	nttion_{is cnmionly a macro=heat{efinedsfa fncAtin Gso.tnore_aheos 
}		f (dkeas_flw__tat eint	 &  !$eas_arg_cntca	 {
			$	y @$ere'ctx= q$e r m  "n";
		$		y $icnt=~$tat eint	_rawines ($ctx;;	
}			frem$y $le= 00;$le=<jtcnt;$le++ =
		$			iere'ctx=.= raw_ine_(line nr,okn)m  "n";
		$		 		$		ARN(?"MACRO_WITH_FLOW_CONTROL)
					    #"Macroi ith $f w_ cnt|rolGtat eint	i toruld b pavoi edn";. j"iere'ctx";;
}		 	
# ceck =or mines cmtinuetion_s'uu sie/ f a#efineds,ioefpro_essr m#, nd 'fsm	
}m elsie{
		$	f ($loefvine =~ /^\..*\\i/{& 		$	   #rine =! /^\s+s*$\#.*\\i/{& 		#(pefpro_essr 		$	   #rine =! /^\s+.*\b(__asm__|asm)\b.*\\i/{& 	#'fsm			$   okines = /^\s+.*\\$/ =
		$		ARN(?"LNE _CONTINUATIONS)
					    #"Avoi aunne_essasyoines cmtinuetion_sn";. jtere'cure;;	m	$ 	}m 		# do {} hile  (0) macro=hs ts:	# tn ge -tat eint	tmacroi do ot pnn_dat tbe=enclosd =inpde hile  (0) loop,	#aiacro=toruld ot pntd ith $a uemicoln_
	$f ($l^V=& @$^V=ge 5.10.0=& 		$   okogllixls{~ /m@/vmineux.lds.h$@=& 		$   okines = /^\.s*\\##s*effness++/nent|?\()?/ {
			$y @$in  0_inesn 

}		y $kcnt=~$rogllct	
		$	y $(toff, $dsat ,okdcnti,kkrs t);
}	fy $lctx= q'';			$($dsat ,okdcnti,kkln,$kcnt, $off;  		cn	ctx_tat eint	_bocak(line nr,okogllct	,$0 ;			$$ctx= q$dsat 
	
		$$dtat a~ s/^\\\n./g;


}		f (dkdtat a= /^\s+s*$##s*effness++/nent|s*$${balanced_prents}s*$do\s${(*)/\s*}s*$hile ss*\?s*$0\*\\)s*\([;\s]*)\s*/) {
			$	y @$stm	i = $2;		$fpy @$semii = $3;	
}			rctx=  //^sn$//^;		$		y $icnt=~$tat eint	_rawines ($ctx;;			$	y @$ere'ctx= q$e r m  "n";
	
}			frem$y $le= 00;$le=<jtcnt;$le++ =
		$			iere'ctx=.= raw_ine_(line nr,okn)m  "n";
		$		 				$	f (d($stm	i =~ tr^;);) {= 01 & 		$		   #rstm	i ! /^\s*$?if|hile |or |sithh_)\b/ {
			$	$ARN(?"SINGLE_STATEMENT_DO_WHILE_MACRO)
				p	     "Sn ge (uat eint	tmacroi toruld ot pse ma do {} hile  (0) loopn";. j"iere'ctx";;
}			 			$	f (defined $ruemii & @$temii es "" {
			$	$ARN(?"DO_WHILE_MACRO_WITH_TRAILING_SEMICOLON)
						    #"do {} hile  (0) macro atoruld ot pbewuemicoln_ ersmnetiedn";. j"iere'ctx";;
}			 		$f elsif ($ldtat a= /^\s+s*$##s*effness++/nent|.*;s*$/) {
			$	rctx=  //^sn$//^;		$		y $icnt=~$tat eint	_rawines ($ctx;;			$	y @$ere'ctx= q$e r m  "n";
	
}			frem$y $le= 00;$le=<jtcnt;$le++ =
		$			iere'ctx=.= raw_ine_(line nr,okn)m  "n";
		$		 				$	ARN(?"TRAILING_SEMICOLON)
					    #"macroi toruld ot pse ma reilbnegatemicoln_n";. j"iere'ctx";;
}		 	}m 		#aiakewurnsasymbol afe_ always wrapped ith $VMLINUX_SYMBOL() ...	#alll=asugnoint	sfyy ceve mrnly on =f ahe firl w_ng: ith $an=asugnoint	:	#	.	#	ALIGN(...)	#	VMLINUX_SYMBOL(...)		$f ($logllixls{eq"'vmineux.lds.h'=& kklne =~ /^(:[(:[^|\s)$nent|s+*=|=s+$/nent|?:[s+|$))) {
		$fARN(?"MISSING_VMLINUX_SYMBOL)
				    #"vmineux.lds.h nn_ds VMLINUX_SYMBOL() argnde C-visible symbol n";. jtere'cure;;
}	}
	# ceck =or mredndean obrckn girgnde f (etc
	cf ($line =~ /^(^*)/\bifsb/=& kk1=~ //lsie\s*$) {
			$y $(tivvel,okntdln,$@chunks;  		cn	ctx_tat eint	_full(line nr,okogllct	,$1;;}m	f#rint " chunks<$#chunks> ine nr<line nr> ntdln<kntdln> ivvel<tivvel>n";

}		#rint " APW: <<$chunks[1][0]>><<$chunks[1][1]>>n";

}		f (dk#chunks > 0=& kklvvelt  0  =
		$		y $@lllw_ed = ();
}	f	y $ial w_  00;
$fppy @$seen  00;
$fppy @$ere'ctx= q$e r m  "n";
		$		y $iin  0_inesn =-01
		$		fremy $kceunk (@chunks; 
		$	p	y $(kcnti,kkbocak { @@{kceunk};
			$	$# If'he fcntitionstcarris  leaing: e winess, he n cnunt=heos  as f fsets.		$	ffy $($hii	eupacs)= $(kcnti'= s^\(?:[s+*\n[+-])*s+*)/s;;}m	$		y $if fset= $tat eint	_rawines ($hii	eupacs)=-01
	
}				/lllw_ed[/lllw_]  00;
$fpp	#rint " COND<$cnti> hii	eupacs<$hii	eupacs> f fset<if fset>n";
		wn$	$# Weceve mlook_daa	tand{ l w_ed{heiswspecfiic ines.		$	ffruupressi_ifbrck s{iin + loffset}{ 01
			$			iere'ctx=.= "$rawines [rin + loffset]\n[...]\n";}m	$		rin + $tat eint	_rawines ($bocak {-01
	
}				hubtrf$lblcak, 0, ength,(lcnti),"'' ;			$	ffrueen++ f ($lblcak = s^\s*${) ;	m		f		#rint " cnti<kcnti> blcak<lblcak>{ l w_ed</lllw_ed[/lllw_]>\n";}m	$		f ($tat eint	_ines ($cnti)=> 1 =
		$				#rint " APW: ALLOWED: cnti<kcnti>\n";}m	$			/lllw_ed[/lllw_]  01;		cm		
	m	$		f ($lblcak = ^\b?:[if|or |hile )sb/ {
			$	$$#rint " APW: ALLOWED: blcak<lblcak>\n";}m	$			/lllw_ed[/lllw_]  01;		cm		
	m	$		f ($tat eint	_bocak_size(ibocak {> 1 =
		$				#rint " APW: ALLOWED: lne s{blcak<lblcak>\n";}m	$			/lllw_ed[/lllw_]  01;		cm		
	m	$		/lllw_++;
}			 			$	f (drueen){
		$	p	y $isum_lllw_ed = 0;
$fpp	fre_ah_{(@lllw_ed {
			$	$	$sum_lllw_ed + $$_;		cm		
	m	$		f ($lsum_lllw_ed = 0  =
		$			$ARN(?"BRACES)
							   # "brck s {} are ot pne_essasyofr many arm f aheis=tat eint	n";. jtere'ctx;;			$	f elsif ($ltum_lllw_ed !=$ial w_ & 		$				@$seen !=$ial w_ =
		$			$CHK("BRACES)
							   #"brck s {} toruld b pused onalll=arms f aheis=tat eint	n";. jtere'ctx;;			$	f 
}			 		$f 	}m 		ff ($!efined $ruupressi_ifbrck s{iinesn =-01} & 		$			line =~ /^\b?if|hile |or |lsie)sb/ {
			$y $ial w_ed= 00;

}		# Ceck =he ioef-cnt|xt .}m	ff ($hubtrf$line , 0, $-[0])=~ /^(\}s*$)/) {
			$	#rint " APW: ALLOWED: oef<$1>\n";}m	$	ial w_ed= 01
		$f 				$y $(tivvel,okntdln,$@chunks;  		cn	ctx_tat eint	_full(line nr,okogllct	,$$-[0]);

}		# Ceck =he icntitions.			$y $(tcnti,kkbocak { @@{kceunks[0]}

}		#rint " CHECKING<line nr> cnti<kcnti> blcak<lblcak>n";

}		f (defined $rcnti)=
			$	hubtrf$lblcak, 0, ength,(lcnti),"'' ;				 		$ff ($tat eint	_ines ($cnti)=> 1 =
		$		#rint " APW: ALLOWED: cnti<kcnti>\n";}m	$	ial w_ed= 01
		$f 	$		f ($lblcak = ^\b?:[if|or |hile )sb/ {
			$	#rint " APW: ALLOWED: blcak<lblcak>\n";}m	$	ial w_ed= 01
		$f 	$		f ($tat eint	_bocak_size(ibocak {> 1 =
		$		#rint " APW: ALLOWED: lne s{blcak<lblcak>\n";}m	$	ial w_ed= 01
		$f 	$		# Ceck =he ioost-cnt|xt .}m	ff ($efined $rchunks[1] =
		$		y $(tcnti,kkbocak { @@{kceunks[1]}

}			f (defined $rcnti)=
			$		hubtrf$lblcak, 0, ength,(lcnti),"'' ;					 			$	f (drblcak = s^\s*$\{/ {
			$	$#rint " APW: ALLOWED: ceunk-1{blcak<lblcak>\n";}m	$		ial w_ed= 01
		$f	 		$f 	}mcf ($livvelt  0 =& kkblcak = s^\s*$\{/ &  !$lllw_ed {
			$	y @$ere'ctx= q$e r m  "n";
		$		y $icnt=~$tat eint	_rawines ($bocak 
	
}			frem$y $le= 00;$le=<jtcnt;$le++ =
		$			iere'ctx=.= raw_ine_(line nr,okn)m  "n";
		$		 				$	ARN(?"BRACES)
					   # "brck s {} are ot pne_essasyofr msn ge (uat eint	tbocaksn";. jtere'ctx;;			$ 	}m 		# ceck =or munne_essasyoblank iness argnde brck s
	$f ($dkine == /^\.s*\}s*$//&& okoefvrawines ~ /^^.\s*N/ ){
		$	CHK("BRACES)
				  # "Blank iness ar_'t wne_essasyobefre_=nmclosdabrck ='}'n";. jtere'oefv;;	}m 		ff ($($rawines ~ /^^.\s*N/=& kkoefvine =~ /^\..*{\s*N/ ){
		$	CHK("BRACES)
				  # "Blank iness ar_'t wne_essasyoater 'an=pen abrck ='{'n";. jtere'oefv;;	}m 	
# no voiaioe s pleas 
}	y $iasm_voiaioe =~$qr
\b(__asm__|asm)\s+(__voiaioe __|voiaioe )sb}

}	f ($line =~ /^\bvoiaioe sb/=& kkine =! /^iasm_voiaioe ) {
		$fARN(?"VOLATILE)
				     "Uie{f avoiaioe =is{usullly{wrong: see Docuint	tion_/voiaioe -cnstidr ed-harmful.txtn";. jtere'cure;;
}	}
	# cntca	ntti_daurfng: ith uu  upacs abetwee_ eleint	i
}	f ($line =~ /^"X+"[A-Z_]+/=|| line =~ /^[A-Z_]+"X+"/ {
			$CHK("CONCATENATED_STRING, 		$	    "Cnsca	ntti_daurfng:i toruld se mupacs abetwee_ eleint	in";. jtere'cure;;
}	}
	# sys_pen /rgld/write/closdaare ot pal w_ed=in he ikernel
}	f ($line =~ /^\b(sys_?:[pen |rgld|write|closd))sb/ {
			$ERROR($FILE_OPS)
				     #"$1=is{inapreorinti_=in kernel cnde.s";. 		fp      sere'cure;;
}	}
	# filp_pen ms alcbackdor mfr msys_pen 
}	f ($line =~ /^\b(filp_pen )sb/ {
			$ERROR($FILE_OPS)
				     #"$1=is{inapreorinti_=in kernel cnde.s";. 		fp      sere'cure;;
}	}
	# rgld[bwl] & write[bwl] se mtoo many1barrisrs, se mte i_relaxed=variat	s			f ($line =~ /^\b(?:[rgld|write)[bwl])sb/ {
			$ERROR($NON_RELAXED_IO)
				     #"Uie{f a$1=is{d'oefca	nd: se m$1_relaxed\n\t;. 		fp      "ith $apreorinti_=memory1barrisrs=nfstead.s";. 		fp      sere'cure;;
}	}
	# likewiss,=nf/uu [bwl] toruld b p__raw_rgld/write[bwl]...		$f ($line =~ /^\b(?in|uu )([bwl]))sb/ {
			$y $($lll,okoeff, $suf)= $(k1,ok2,ok3 ;			$$oeff=  //^i /rgld/;			$$oeff=  //^uu /write/;			$ERROR($NON_RELAXED_IO)
				     #"Uie{f a$lll=is{d'oefca	nd: se m;. 		fp      "__raw_$oeff$suf\n\t;. 		fp      "ith $apreorinti_=memory1barrisrs=nfstead.s";. 		fp      sere'cure;;
}	}
	# dsb=is{too ARMish, nd 'toruld seullly{be=mb.		$f ($line =~ /^[^-_>*\.]\bdsb\b[^-_\.;]) {
		$fARN(?"ARM_BARRIER)
				     "Uie{f adsb=is{discourangnd: p efr =mb.s";. 		fp     sere'cure;;
}	}
	# MSM=-0ceck =iftamnon board-gpiomux ixls{ha =any gpiomux declartion_i		f ($logllixls{~ //\/mah_-msm\/board-[0-9]+/{& 		   okogllixls{~ //camdra/=& kkogllixls{~ //gpiomux/{& 		   okine =~ /^\s*truct| msm_gpiomux_cntfigss*)& {
		$ARN(?"GPIOMUX_IN_BOARD)
			     "Non gpiomux board ixls{canot peve ma gpiomux cntfig declartion_i. Pleas  declare gpiomux cntfigs=nf board-*-gpiomux.c ixls.n";. jtere'cure;;
}}
	# MSM=-0ceck =iftvreg_xx