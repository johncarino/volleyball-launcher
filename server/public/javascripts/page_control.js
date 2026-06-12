"use strict";
// Page Control module - handles page navigation

var activePageId = "page-main";

$(document).ready(function () {
	setupPageNavigation();
});

function setupPageNavigation() {
	$(".page-btn").click(function () {
		showPage($(this).attr("data-page-target"));
	});

	showPage(activePageId);
}

function showPage(pageId) {
	activePageId = pageId;
	$(".page").removeClass("active");
	$(".page-btn").removeClass("active");
	$("#" + pageId).addClass("active");
	$(".page-btn[data-page-target='" + pageId + "']").addClass("active");
}
