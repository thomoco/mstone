# The contents of this file are subject to the Netscape Public
# License Version 1.1 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of
# the License at http://www.mozilla.org/NPL/
#
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
#
# The Original Code is the Netscape Mailstone utility,
# released March 17, 2000.
#
# The Initial Developer of the Original Code is Netscape
# Communications Corporation. Portions created by Netscape are
# Copyright (C) 1997-2000 Netscape Communications Corporation. All
# Rights Reserved.
#
# Contributor(s):	Dan Christian <DanChristian65@gmail.com>
#			Marcel DePaolis <marcel@netcape.com>
#
# Alternatively, the contents of this file may be used under the
# terms of the GNU Public License (the "GPL"), in which case the
# provisions of the GPL are applicable instead of those above.
# If you wish to allow use of your version of this file only
# under the terms of the GPL and not to allow others to use your
# version of this file under the NPL, indicate your decision by
# deleting the provisions above and replace them with the notice
# and other provisions required by the GPL.  If you do not delete
# the provisions above, a recipient may use your version of this
# file under either the NPL or the GPL.
#####################################################

# This file defines the structures that hold summary, client, and graph data,

# This sets the names used for display.  Can be internationalized.
%fieldNames
    = (
       #internal name,	Printed name
       "Try",		=> [ "Try",    "number of attempts", ""],
       "Error",		=> [ "Error",  "number of errors", ""],
       "BytesR",	=> [ "BytesR", "total bytes read by client(s)", ""],
       "BytesW",	=> [ "BytesW", "total bytes written by client(s)", ""],
       "Time",		=> [ "Time",   "mean time per try", ""],
       "TimeMin",	=> [ "TMin",   "smallest time of all try", ""],
       "TimeMax",	=> [ "TMax",   "largest time of all tries", ""],
       "Time2",		=> [ "TStd",   "standard deviation of try times", ""],
       );

# This sets the names used for display.  Can be internationalized.
# All top level names are here (both timers and scalars).
# Any unlisted names will map to themselves.
%timerNames
    = (
    #internal name => [ Printed name, title text, doc url ]
    "total"       => [ "total",       "sum of all timers" ],
    "conn"        => [ "connect",     "connections to server" ],
    "reconn"      => [ "reconnect",   "re-connections to server" ],
    "banner"      => [ "banner",      "read banner from server" ],
    "login"       => [ "login",       "login to an account" ],
    "cmd"         => [ "command",     "miscellaneous command" ],
    "submit"      => [ "submit",      "submit a message" ],
    "retrieve"    => [ "retrieve",    "retrieve a message" ],
    "logout"      => [ "logout",      "logout of an account" ],
    "idle"        => [ "idle",        "mstone delay" ],
    "connections" => [ "connections", "current connection count" ],
    "blocks"      => [ "blocks",      "mstone blocks completed" ],
    );

%p4TimerNames
    = (
    #internal name => [ Printed name, title text, doc url ]
    "total"       => [ "total",       "sum of all timers", ""],
    "conn"        => [ "client",      "setup client", ""],
    "checkout"    => [ "client_sync", "initial client sync", ""],
    "add"         => [ "add",         "add a file", ""],
    "del"         => [ "delete",      "delete a file", ""],
    "mkdir"       => [ "add_dir",     "add a directory (with small file)", ""],
    "modify"      => [ "edit",        "open a file for editing", ""],
    "list"        => [ "files+dirs",  "list files and dirs from server", ""],
    "diff"        => [ "diff",        "get unified diff of file", ""],
    "log"         => [ "filelog",     "get log of a file", ""],
    "blame"       => [ "annotate",    "annotate all changes to a file", ""],
    "stat"        => [ "opened",      "list opened files in client", ""],
    "update"      => [ "sync",        "sync with depot head", ""],
    "commit"      => [ "submit",      "(attempt to) submit changes", ""],
    "logout"      => [ "client_del",  "delete client", ""],
    "idle"        => [ "idle",        "mstone delay", ""],
    "connections" => [ "connections", "current connection count", ""],
    "blocks"      => [ "blocks",      "mstone blocks completed", ""],
    );

%svnTimerNames
    = (
    #internal name => [ Printed name, title text, doc url ]
    "total"       => [ "total",       "sum of all timers", ""],
    "conn"        => [ "get_head",    "read head revision", ""],
    "checkout"    => [ "checkout",    "checkout client", ""],
    "add"         => [ "add",         "add a file", ""],
    "del"         => [ "delete",      "delete a file", ""],
    "mkdir"       => [ "mkdir",       "add a directory", ""],
    "modify"      => [ "modify",      "change a file (no SVN)", ""],
    "list"        => [ "list",        "list files from server", ""],
    "diff"        => [ "diff",        "get unified diff of file", ""],
    "log"         => [ "log",         "get log of a file", ""],
    "blame"       => [ "blame",       "annotate each line in a file", ""],
    "stat"        => [ "stat",        "list opened files in client", ""],
    "update"      => [ "update",      "update to head revision", ""],
    "commit"      => [ "commit",      "(attempt to) commit changes", ""],
    "logout"      => [ "logout",      "delete client (no SVN)", ""],
    "idle"        => [ "idle",        "mstone delay", ""],
    "connections" => [ "connections", "current connection count", ""],
    "blocks"      => [ "blocks",      "mstone blocks completed", ""],
    );

# Mapping for each protocol to reporting conversions
%reportMap
    = (
       "SVN" 	=> \%svnTimerNames,
       "P4" 	=> \%p4TimerNames,
    );

# hold time graphs for each protocol
%graphs = ();

# Totals are done during plotting, if needed
%finals = ();			# create base finals hash

# These are sections that dont get passed to mailclient (case insensitive)
@scriptWorkloadSections
    = (
       "Config",		# special, references %params
       "Client",		# testbed client(s)
       "Graph",			# graph generation
       "Setup",			# things to run with ./setup
       "Startup",		# things to run before test
       "Monitor",		# other performance monitoring
       "PreTest",		# things to run before test
       "PostTest",		# things to run after test
       );

# These are sections that arent protocols.  Anything else must be.
@nonProtocolSections
    = (@scriptWorkloadSections, ("Default"));

# These are the known workload parameters (as they will print)
# These are coerced to upper case internally (do NOT internationize)
@workloadParameters
    = (
       "addressFormat",
       "arch",
       "blockID",
       "blockTime",
       "chartHeight",
       "chartPoints",
       "chartWidth",
       "clientCount",
       "command",
       "comments",
       "file",
       "firstAddress",
       "firstLogin",
       "frequency",
       "gnuplot",
       "group",
       "idleTime",
       "leaveMailOnServer",
       "loginFormat",
       "loopDelay",
       "numAddresses",
       "numLogins",
       "numLoops",
       "numRecips",
       "mailClient",
       "maxBlocks",
       "maxClients",
       "maxErrors",
       "maxThreads",
       "maxProcesses",
       "passwdFormat",
       "processes",
       "rampTime",
       "rcp",
       "rsh",
       "sequentialAddresses",
       "sequentialLogins",
       "server",
       "smtpMailFrom",
       "sysConfig",
       "threads",
       "telemetry",
       "tempDir",
       "time",
       "title",
       "TStamp",
       "useAuthLogin",
       "useEHLO",
       "weight",
       "wmapBannerCmds",
       "wmapClientHeader",
       "wmapInBoxCmds",
       "wmapLoginCmd",
       "wmapLoginData",
       "wmapLogoutCmds",
       "wmapMsgReadCmds",
       "wmapMsgWriteCmds",
       "workload",
       );
       
# Some protocols implicitly close the connection on a error, but others don't.
# We list every protocol we know and which errors will close the connection.
# If any error will close, just use "total".
# If no errors will close, use an empty list.
# TODO: All protocols have not been verified for correctness.
%closeOnErrorProtocols
  = (
     "HTTP" 	=> [ "total" ],
     "IMAP4" 	=> [ "total" ],
     "MULTIPOP"	=> [ "total" ],
     "POP3" 	=> [ "total" ],
     "SMTP" 	=> [ "total" ],
     "WEB" 	=> [ "total" ],
     "WMAP" 	=> [ "total" ],
     "SVN" 	=> [ "checkout" ],
     "P4" 	=> [ "conn", "checkout" ],
     );

# Expand timer name to report text
# Usage: expandTimerHtml(timer, protocol="")
sub expandTimerText {
    my $t = shift;
    my $proto = shift || "";
    my $map = \%timerNames;     # default timer name mapping

    if (($proto) && $reportMap{$proto} && $reportMap{$proto}) {
        $map = $reportMap{$proto};
    }
    my $txt = $t;               # default
    if (defined $map->{$t}) {
        my @tfields = @{ $map->{$t} };
        $txt = $tfields[0];     # [ text, title, url ]
    }
    #print "Expanding timer '$proto':'$t' -> '$txt'\n"; # DEBUG
    return $txt;
}

# Expand timer name to a URL reference
# Usage: expandTimerHtml(timer, protocol="", section="")
sub expandTimerHtml {
    my $t = shift;
    my $proto = shift || "";
    my $section = shift || "";
    my $map = \%timerNames;     # default timer name mapping

    if (($proto) && $reportMap{$proto} && $reportMap{$proto}) {
        $map = $reportMap{$proto};
    }
    my $txt = $t;               # default
    my $help = "";
    my $url = "";
    if (defined $map->{$t}) {
        my @tfields = @{ $map->{$t} };
        $txt = $tfields[0];     # [ text, title, url ]
        if (defined $tfields[1]) {
            $help = $tfields[1];
            if (defined $tfields[2]) {
                $url = $tfields[2];
                $url = lc($proto) . lc($txt) unless ($url); # empty gets default
            }
        }
    }
    #print "Expanding timer '$proto':'$t' -> '$txt' '$help' '$url'\n"; # DEBUG
    my $msg = "<A NAME=\"$section$proto$t\"";
    $msg .= " TITLE=\"$help\"" if ($help);
    $msg .= " HREF=\"../$mstoneURL#$url\"" if ($url);
    $msg .= ">$txt</A>";
    return $msg;
}

return 1;

# Expand field name to report text
# Usage: expandFieldHtml(timer)
sub expandFieldText {
    my $t = shift;
    my $map = \%fieldNames;     # default field name mapping
    my $txt = $t;               # default
    if (defined $map->{$t}) {
        my @tfields = @{ $map->{$t} };
        $txt = $tfields[0];     # [ text, title, url ]
    }
    #print "Expanding field '$t' -> '$txt'\n"; # DEBUG
    return $txt;
}

# Expand field name to a URL reference
# Usage: expandFieldHtml(field, section="")
sub expandFieldHtml {
    my $t = shift;
    my $section = shift || "";
    my $map = \%fieldNames;     # default field name mapping
    my $txt = $t;               # default
    my $help = "";
    my $url = "";
    if (defined $map->{$t}) {
        my @tfields = @{ $map->{$t} };
        $txt = $tfields[0];     # [ text, title, url ]
        if (defined $tfields[1]) {
            $help = $tfields[1];
            if (defined $tfields[2]) {
                $url = $tfields[2];
                $url = lc($txt) unless ($url); # empty gets default
            }
        }
    }
    #print "Expanding field '$t' -> '$txt' '$help' '$url'\n"; # DEBUG
    my $msg = "<A NAME=\"$section$t\"";
    $msg .= " TITLE=\"$help\"" if ($help);
    $msg .= " HREF=\"../$mstoneURL#$url\"" if ($url);
    $msg .= ">$txt</A>";
    return $msg;
}

return 1;
