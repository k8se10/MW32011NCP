// ListProjectFiles.java -- lists every DomainFile in the current project, recursively.
import ghidra.app.script.GhidraScript;
import ghidra.framework.model.DomainFile;
import ghidra.framework.model.DomainFolder;

public class ListProjectFiles extends GhidraScript {
    @Override
    protected void run() throws Exception {
        DomainFolder root = state.getProject().getProjectData().getRootFolder();
        walk(root);
    }

    private void walk(DomainFolder folder) throws Exception {
        println("FOLDER: " + folder.getPathname());
        for (DomainFile f : folder.getFiles()) {
            println("  FILE: " + f.getPathname() + "  (contentType=" + f.getContentType() + ")");
        }
        for (DomainFolder sub : folder.getFolders()) {
            walk(sub);
        }
    }
}
