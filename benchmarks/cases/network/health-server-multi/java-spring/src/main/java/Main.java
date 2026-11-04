import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;

@SpringBootApplication
public class Main {
    public static void main(String[] args) { SpringApplication.run(Main.class, args); }
    @RestController
    static class Health {
        @GetMapping(value="/healthz", produces=MediaType.TEXT_PLAIN_VALUE)
        String healthz() { return "ok"; }
        @GetMapping(value="/", produces=MediaType.TEXT_PLAIN_VALUE)
        String root() { return "health-server ok"; }
        @RequestMapping(value="/**", produces=MediaType.TEXT_PLAIN_VALUE)
        String notfound() { throw new ResponseStatusException(HttpStatus.NOT_FOUND, "not found"); }
    }
}
