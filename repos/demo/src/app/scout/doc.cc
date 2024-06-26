/*
 * \brief  Browser content
 * \date   2010-11-07
 * \author Generated by GOSH
 */

/*
 * Copyright (C) 2010-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include "elements.h"
#include "styles.h"

using namespace Scout;

Document *create_document()
{
	Document *doc = new Document();
	doc->title = "Introduction to Genode";

	/**
	 * Table of contents
	 */

	Chapter *toc = new Chapter();
	Center *tc = new Center();
	tc->append(new Spacer(1, 20));

	/* anchor for section "The launchpad application starter" */
	Anchor *anchor0 = new Anchor();

	Block *b0 = new Block();
	b0->append_linktext("The launchpad application starter", &link_style, anchor0);
	tc->append(b0);

	/* anchor for section "Recursive system structure" */
	Anchor *anchor1 = new Anchor();

	b0 = new Block();
	b0->append_linktext("Recursive system structure", &link_style, anchor1);
	tc->append(b0);

	/* anchor for section "The flexibility of nested policies" */
	Anchor *anchor2 = new Anchor();

	b0 = new Block();
	b0->append_linktext("The flexibility of nested policies", &link_style, anchor2);
	tc->append(b0);

	/* anchor for section "Where to go from here?" */
	Anchor *anchor3 = new Anchor();

	b0 = new Block();
	b0->append_linktext("Where to go from here?", &link_style, anchor3);
	tc->append(b0);
	toc->append(tc);
	toc->append(new Spacer(1, 20));
	doc->toc = toc;
	doc->append(new Spacer(1, 10));
	Block *title = new Block(Block::CENTER);
	title->append_plaintext("Introduction to Genode", &chapter_style);
	doc->append(new Center(title));

	extern char _binary_genode_logo_png_start[];
	Png_image *png = new Png_image(_binary_genode_logo_png_start);
	doc->append(new Spacer(1, 10));
	doc->append(new Center(png));
	doc->append(new Spacer(1, 10));

	b0 = new Block();
	b0->append_plaintext("Genode is a construction kit for building special-purpose operating systems", &plain_style);
	b0->append_plaintext("out of a number of components such as device drivers, protocol", &plain_style);
	b0->append_plaintext("stacks, and applications. Those components are organized using only a few", &plain_style);
	b0->append_plaintext("yet powerful architectual prinicples, and thereby, allow for the", &plain_style);
	b0->append_plaintext("composition of a wide range of different systems.", &plain_style);
	doc->append(b0);

	b0 = new Block();
	b0->append_plaintext("The following introduction will provide you with hands-on experience with", &plain_style);
	b0->append_plaintext("the basics of Genode:", &plain_style);
	doc->append(b0);

	Item *i0 = new Item(&plain_style, " o", 20);

	Block *b1 = new Block();
	b1->append_plaintext("The creation and destruction of single processes as well as arbitrarily", &plain_style);
	b1->append_plaintext("complex sub systems", &plain_style);
	i0->append(b1);
	doc->append(i0);

	i0 = new Item(&plain_style, " o", 20);

	b1 = new Block();
	b1->append_plaintext("The trusted-path facility of the Nitpicker secure GUI", &plain_style);
	i0->append(b1);
	doc->append(i0);

	i0 = new Item(&plain_style, " o", 20);

	b1 = new Block();
	b1->append_plaintext("The assignment of resource quotas to sub systems", &plain_style);
	i0->append(b1);
	doc->append(i0);

	i0 = new Item(&plain_style, " o", 20);

	b1 = new Block();
	b1->append_plaintext("The multiple instantiation of services", &plain_style);
	i0->append(b1);
	doc->append(i0);

	i0 = new Item(&plain_style, " o", 20);

	b1 = new Block();
	b1->append_plaintext("The usage of run-time adaptable policy for routing client requests to", &plain_style);
	b1->append_plaintext("different services", &plain_style);
	i0->append(b1);
	doc->append(i0);

	/**
	 * Chapter "The launchpad application starter"
	 */

	Chapter *chapter = new Chapter();
	chapter->append(anchor0);
	chapter->append(new Spacer(1, 20));

	b0 = new Block();
	b0->append_plaintext("The launchpad application starter", &chapter_style);
	chapter->append(b0);

	extern char _binary_launchpad_png_start[];
	png = new Png_image(_binary_launchpad_png_start);
	chapter->append(new Spacer(1, 10));
	chapter->append(new Center(png));
	chapter->append(new Spacer(1, 10));

	b0 = new Block();
	b0->append_plaintext("Figure", &plain_style);
	b0->append_plaintext("launchpad", &link_style);
	b0->append_plaintext("shows the main window of the launchpad application. It", &plain_style);
	b0->append_plaintext("consists of three areas. The upper area contains status information about", &plain_style);
	b0->append_plaintext("launchpad itself. The available memory quota is presented by a grey-colored", &plain_style);
	b0->append_plaintext("bar. The middle area of the window contains the list of available applications", &plain_style);
	b0->append_plaintext("that can be started by clicking on the application's name. Before starting an", &plain_style);
	b0->append_plaintext("application, the user can define the amount of memory quota to donate to the", &plain_style);
	b0->append_plaintext("new application by adjusting the red bar using the mouse.", &plain_style);
	Launcher *l0 = new Launcher("launchpad", 1, 100000, 36*1024*1024);
	b0->append_launchertext("Start the launchpad by clicking on this link...", &link_style, l0);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("For a first test, you may set the memory quota of the program named scout to", &plain_style);
	b0->append_plaintext("10MB and then click its name. Thereupon, another instance of the scout text", &plain_style);
	b0->append_plaintext("browser will be started and the lower area of launchpad becomes populated with", &plain_style);
	b0->append_plaintext("status information about launchpad's children. Currently, launchpad has scout", &plain_style);
	b0->append_plaintext("as its only child. For each child, its name, its memory quota, and a kill", &plain_style);
	b0->append_plaintext("button are presented.  After having started scout, you will further notice a", &plain_style);
	b0->append_plaintext("change of launchpad's own status information as the memory quota spent for", &plain_style);
	b0->append_plaintext("scout is not directly available to launchpad anymore.", &plain_style);
	chapter->append(b0);

	extern char _binary_setup_png_start[];
	png = new Png_image(_binary_setup_png_start);
	chapter->append(new Spacer(1, 10));
	chapter->append(new Center(png));
	chapter->append(new Spacer(1, 10));

	b0 = new Block();
	b0->append_plaintext("In Figure", &plain_style);
	b0->append_plaintext("setup", &link_style);
	b0->append_plaintext(", you see an illustration of the current setup (slightly", &plain_style);
	b0->append_plaintext("simplified). At", &plain_style);
	b0->append_plaintext("the very bottom, there are the kernel, core, and init. Init has started the", &plain_style);
	b0->append_plaintext("framebuffer driver, the timer driver, the nitpicker GUI server, and launchpad", &plain_style);
	b0->append_plaintext("as it children. Launchpad, in turn, has started the second instance of scout as", &plain_style);
	b0->append_plaintext("its only child. You can get a further idea about the relationship between the", &plain_style);
	b0->append_plaintext("applications by pressing the", &plain_style);
	b0->append_plaintext("ScrLock", &mono_style);
	b0->append_plaintext("key, which gets especially handled by", &plain_style);
	b0->append_plaintext("the nitpicker GUI server. We call this key the X-ray key because it makes the", &plain_style);
	b0->append_plaintext("identity of each window on screen visible to the user. Each screen region gets", &plain_style);
	b0->append_plaintext("labeled by its chain of parents and their grandparents respectively. During the", &plain_style);
	b0->append_plaintext("walk through the demo scenario, you may press the X-ray key at any time to make", &plain_style);
	b0->append_plaintext("the parent-child relationships visible on screen.", &plain_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("By pressing the kill button (labeled with", &plain_style);
	b0->append_plaintext("x", &mono_style);
	b0->append_plaintext(") of the scout child in", &plain_style);
	b0->append_plaintext("launchpad's window, scout will disappear and launchpad regains its original", &plain_style);
	b0->append_plaintext("memory quota. Although killing a process may sound like a simple thing to do,", &plain_style);
	b0->append_plaintext("it is worthwhile to mention that scout was using a number of services, for", &plain_style);
	b0->append_plaintext("example core's LOG service, the nitpicker GUI service, and the timer service.", &plain_style);
	b0->append_plaintext("While using these services, scout made portions of its own memory quota", &plain_style);
	b0->append_plaintext("available to them. When scout was killed by launchpad, all those relationships", &plain_style);
	b0->append_plaintext("were gracefully reverted such that there is no resource leakage.", &plain_style);
	chapter->append(b0);

	Navbar *navbar = new Navbar();
	navbar->prev_link("Home", doc);
	navbar->next_link("Recursive system structure", anchor1);
	chapter->append(navbar);

	/**
	 * Chapter "Recursive system structure"
	 */

	chapter = new Chapter();
	chapter->append(anchor1);
	chapter->append(new Spacer(1, 20));

	b0 = new Block();
	b0->append_plaintext("Recursive system structure", &chapter_style);
	chapter->append(b0);

	extern char _binary_x_ray_small_png_start[];
	png = new Png_image(_binary_x_ray_small_png_start);
	chapter->append(new Spacer(1, 10));
	chapter->append(new Center(png));
	chapter->append(new Spacer(1, 10));

	b0 = new Block();
	b0->append_plaintext("Thanks to the recursive structure of Genode, the mechanisms", &plain_style);
	b0->append_plaintext("that function for a single application are also applicable to", &plain_style);
	b0->append_plaintext("whole sub systems.", &plain_style);
	b0->append_plaintext("As a test, you may configure the launchpad application", &plain_style);
	b0->append_plaintext("entry within the launchpad window to 15MB and start", &plain_style);
	b0->append_plaintext("another instance of launchpad.", &plain_style);
	b0->append_plaintext("A new launchpad window will appear. Apart from the status", &plain_style);
	b0->append_plaintext("information at the upper part of its window, it looks", &plain_style);
	b0->append_plaintext("completely identical to the first instance.", &plain_style);
	b0->append_plaintext("You may notice that the displayed available quota of the", &plain_style);
	b0->append_plaintext("second launchpad instance is lower then the 15MB. The", &plain_style);
	b0->append_plaintext("difference corresponds to the application's static memory", &plain_style);
	b0->append_plaintext("usage including the BSS segment and the double-buffer", &plain_style);
	b0->append_plaintext("backing store.", &plain_style);
	b0->append_plaintext("With the new instance, you may start further applications,", &plain_style);
	b0->append_plaintext("for example by clicking on", &plain_style);
	b0->append_plaintext("testnit.", &mono_style);
	b0->append_plaintext("To distinguish the different instances of the applications", &plain_style);
	b0->append_plaintext("on screen, the X-ray key becomes handy again.", &plain_style);
	b0->append_plaintext("Figure", &plain_style);
	b0->append_plaintext("x-ray_small", &link_style);
	b0->append_plaintext("shows a screenshot of the described setup", &plain_style);
	b0->append_plaintext("in X-ray mode.", &plain_style);
	b0->append_plaintext("Now, after creating a whole hierarchy of applications,", &plain_style);
	b0->append_plaintext("you can try killing the whole tree at once by clicking", &plain_style);
	b0->append_plaintext("the kill button of the launchpad entry in the original", &plain_style);
	b0->append_plaintext("launchpad window.", &plain_style);
	b0->append_plaintext("You will notice that whole sub system gets properly", &plain_style);
	b0->append_plaintext("destructed and the original system state is regained.", &plain_style);
	chapter->append(b0);

	navbar = new Navbar();
	navbar->prev_link("The launchpad application starter", anchor0);
	navbar->next_link("The flexibility of nested policies", anchor2);
	chapter->append(navbar);

	/**
	 * Chapter "The flexibility of nested policies"
	 */

	chapter = new Chapter();
	chapter->append(anchor2);
	chapter->append(new Spacer(1, 20));

	b0 = new Block();
	b0->append_plaintext("The flexibility of nested policies", &chapter_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("Beside providing the ability to construct and destruct", &plain_style);
	b0->append_plaintext("hierarchically structured sub systems, the recursive", &plain_style);
	b0->append_plaintext("system structure allows for an extremely flexible", &plain_style);
	b0->append_plaintext("definition and management of system policies that can", &plain_style);
	b0->append_plaintext("be implanted into each parent.", &plain_style);
	b0->append_plaintext("As an example, launchpad has a simple built-in policy of how", &plain_style);
	b0->append_plaintext("children are connected to services.", &plain_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("If a child requests", &plain_style);
	b0->append_plaintext("a service, launchpad looks if such a service is provided", &plain_style);
	b0->append_plaintext("by any of the other children and, if so, a connection", &plain_style);
	b0->append_plaintext("gets established. If the service is not offered by any child,", &plain_style);
	b0->append_plaintext("launchpad delegates the request to its parent.", &plain_style);
	b0->append_plaintext("For example, a request for the", &plain_style);
	b0->append_plaintext("LOG", &mono_style);
	b0->append_plaintext("service will always", &plain_style);
	b0->append_plaintext("end up at core, which implements the service by the", &plain_style);
	b0->append_plaintext("means of terminal (or kernel debug) output.", &plain_style);
	b0->append_plaintext("By starting a child that offers the same service interface,", &plain_style);
	b0->append_plaintext("however, we can shadow core's", &plain_style);
	b0->append_plaintext("LOG", &mono_style);
	b0->append_plaintext("service by an alternative", &plain_style);
	b0->append_plaintext("implementation.", &plain_style);
	b0->append_plaintext("You can try this out by first starting", &plain_style);
	b0->append_plaintext("testnit", &mono_style);
	b0->append_plaintext("and", &plain_style);
	b0->append_plaintext("observing its log output at the terminal window. When", &plain_style);
	b0->append_plaintext("started,", &plain_style);
	b0->append_plaintext("testnit", &mono_style);
	b0->append_plaintext("tells us some status information.", &plain_style);
	b0->append_plaintext("By further starting the program called", &plain_style);
	b0->append_plaintext("nitlog,", &mono_style);
	b0->append_plaintext("we create", &plain_style);
	b0->append_plaintext("a new", &plain_style);
	b0->append_plaintext("LOG", &mono_style);
	b0->append_plaintext("service as a child of launchpad. On screen, this", &plain_style);
	b0->append_plaintext("application appears just as a black window that can be", &plain_style);
	b0->append_plaintext("dragged to any screen position with the mouse.", &plain_style);
	b0->append_plaintext("When now starting a new instance of", &plain_style);
	b0->append_plaintext("testnit", &mono_style);
	b0->append_plaintext(", launchpad", &plain_style);
	b0->append_plaintext("will resolve the request for the", &plain_style);
	b0->append_plaintext("LOG", &mono_style);
	b0->append_plaintext("service by establishing", &plain_style);
	b0->append_plaintext("a connection to", &plain_style);
	b0->append_plaintext("nitlog", &mono_style);
	b0->append_plaintext("instead of propagating the request", &plain_style);
	b0->append_plaintext("to its parent. Consequently, we can now observe the status", &plain_style);
	b0->append_plaintext("output of the second", &plain_style);
	b0->append_plaintext("testnit", &mono_style);
	b0->append_plaintext("instance inside the", &plain_style);
	b0->append_plaintext("nitlog", &mono_style);
	b0->append_plaintext("window.", &plain_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("The same methodology can be applied to arbitrarily complex", &plain_style);
	b0->append_plaintext("services. For example, you can create a new instance of", &plain_style);
	b0->append_plaintext("the framebuffer service by starting the", &plain_style);
	b0->append_plaintext("liquid_fb", &mono_style);
	b0->append_plaintext("application.", &plain_style);
	b0->append_plaintext("This application provides the framebuffer service and,", &plain_style);
	b0->append_plaintext("in turn, uses the nitpicker GUI server to get displayed on", &plain_style);
	b0->append_plaintext("screen. Because any new requests for a framebuffer will now be", &plain_style);
	b0->append_plaintext("served by the", &plain_style);
	b0->append_plaintext("liquid_fb", &mono_style);
	b0->append_plaintext("application, we can start another", &plain_style);
	b0->append_plaintext("instance of nitpicker. This instance uses", &plain_style);
	b0->append_plaintext("liquid_fb", &mono_style);
	b0->append_plaintext("as its", &plain_style);
	b0->append_plaintext("graphics back end and, in turn, provides the GUI service.", &plain_style);
	b0->append_plaintext("Now, when starting another instance of scout, the new scout", &plain_style);
	b0->append_plaintext("window will appear within", &plain_style);
	b0->append_plaintext("liquid_fb", &mono_style);
	b0->append_plaintext("too (Figure", &plain_style);
	b0->append_plaintext("liquid_fb_small", &link_style);
	b0->append_plaintext(").", &plain_style);
	chapter->append(b0);

	extern char _binary_liquid_fb_small_png_start[];
	png = new Png_image(_binary_liquid_fb_small_png_start);
	chapter->append(new Spacer(1, 10));
	chapter->append(new Center(png));
	chapter->append(new Spacer(1, 10));

	b0 = new Block();
	b0->append_plaintext("The extremely simple example policy implemented in launchpad", &plain_style);
	b0->append_plaintext("in combination with the recursive system structure of Genode", &plain_style);
	b0->append_plaintext("already provides a wealth of flexibility without the need", &plain_style);
	b0->append_plaintext("to recompile or reconfigure any application.", &plain_style);
	b0->append_plaintext("The policy implemented and enforced by a parent may", &plain_style);
	b0->append_plaintext("also deny services for its children or impose other restrictions.", &plain_style);
	b0->append_plaintext("For example, the window labels presented in X-ray mode are", &plain_style);
	b0->append_plaintext("successively defined by all parents and grandparents that", &plain_style);
	b0->append_plaintext("mediate the request of an application to the GUI service.", &plain_style);
	b0->append_plaintext("The scout window as the parent of launchpad imposes its", &plain_style);
	b0->append_plaintext("policy of labeling the GUI session with the label", &plain_style);
	b0->append_plaintext("\"launchpad\"", &italic_style);
	b0->append_plaintext(".", &plain_style);
	b0->append_plaintext("Init as the parent of scout again overrides this label", &plain_style);
	b0->append_plaintext("with the name of its immediate child from which the GUI request", &plain_style);
	b0->append_plaintext("comes from. Hence the label becomes", &plain_style);
	b0->append_plaintext("\"scout -> launchpad\"", &italic_style);
	b0->append_plaintext(".", &plain_style);
	chapter->append(b0);

	navbar = new Navbar();
	navbar->prev_link("Recursive system structure", anchor1);
	navbar->next_link("Where to go from here?", anchor3);
	chapter->append(navbar);

	/**
	 * Chapter "Where to go from here?"
	 */

	chapter = new Chapter();
	chapter->append(anchor3);
	chapter->append(new Spacer(1, 20));

	b0 = new Block();
	b0->append_plaintext("Where to go from here?", &chapter_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("Although this little demonstration scratches only the surface of", &plain_style);
	b0->append_plaintext("Genode, we hope that the power of its underlying design becomes", &plain_style);
	b0->append_plaintext("apparent. The most distinctive property of Genode, however, is its", &plain_style);
	b0->append_plaintext("extremely low complexity. The functionality of the complete demo", &plain_style);
	b0->append_plaintext("scenario is implemented in less than 20,000 lines of source code", &plain_style);
	b0->append_plaintext("(LOC), including the GUI and the demo applications. As a point of", &plain_style);
	b0->append_plaintext("reference, when relying on libpng for decompressing the images as seen", &plain_style);
	b0->append_plaintext("in the text browser, this number doubles. In fact, the complete base", &plain_style);
	b0->append_plaintext("OS framework accounts for less source-code complexity than the code", &plain_style);
	b0->append_plaintext("needed for decoding the PNG images. To these numbers, the complexity", &plain_style);
	b0->append_plaintext("of the used underlying kernel must be added, for example 10-20 KLOC", &plain_style);
	b0->append_plaintext("for an L4 microkernel (or far more than 500 KLOC when relying on the", &plain_style);
	b0->append_plaintext("Linux kernel). In combination with a microkernel, Genode enables the", &plain_style);
	b0->append_plaintext("implementation of security-sensitive applications with a trusted", &plain_style);
	b0->append_plaintext("computing base (TCB) of some thousands rather than millions of lines", &plain_style);
	b0->append_plaintext("of code. If using a hypervisor as kernel for Genode, this advantage", &plain_style);
	b0->append_plaintext("can further be combined with compatibility to existing applications", &plain_style);
	b0->append_plaintext("executed on virtual machines.", &plain_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("More details, architectural and technical documents, our road", &plain_style);
	b0->append_plaintext("map, and the complete source code are available at", &plain_style);
	b0->append_plaintext("https://genode.org", &link_style);
	b0->append_plaintext(".", &plain_style);
	chapter->append(b0);

	b0 = new Block();
	b0->append_plaintext("The development of the Genode OS Framework is conducted as", &plain_style);
	b0->append_plaintext("an open-source community project, coordinated by Genode Labs,", &plain_style);
	b0->append_plaintext("a company founded by the original authors of Genode.", &plain_style);
	b0->append_plaintext("If you are interested in supporting our project through", &plain_style);
	b0->append_plaintext("participation or funding, please consider joining our", &plain_style);
	b0->append_plaintext("community (", &plain_style);
	b0->append_plaintext("https://genode.org", &link_style);
	b0->append_plaintext(") or contact Genode Labs", &plain_style);
	b0->append_plaintext("(", &plain_style);
	b0->append_plaintext("https://www.genode-labs.com", &link_style);
	b0->append_plaintext(").", &plain_style);
	chapter->append(b0);

	Verbatim *v0 = new Verbatim(verbatim_bgcol);
	v0->append_textline(" info@genode-labs.com", &mono_style);
	chapter->append(v0);

	navbar = new Navbar();
	navbar->prev_link("The flexibility of nested policies", anchor2);
	chapter->append(navbar);

	navbar = new Navbar();
	navbar->next_link("The launchpad application starter", anchor0);
	doc->append(navbar);

	return doc;
}
